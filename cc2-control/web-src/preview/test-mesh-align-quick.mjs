// Mesh-guided screw alignment, the quick check: four probes of the Screws tab less the plate shape of the
// saved full mesh of the mounted plate. It refuses a missing, foreign-temperature or old full mesh, warns
// about old probes and about a twist, and sends nothing to the printer. All data is synthetic.
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS, analyseScrewProbes, meshAt } from '../src/lib/mesh-align.ts'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
const number = (value, digits = 3) =>
  new Intl.NumberFormat('en', { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(Math.abs(value))
const signed = value => `${value > 0 ? '+' : value < 0 ? '−' : ''}${number(value)}`
let browser
const errors = []
const writes = []
const shots = path.join(root, 'preview/artifacts')
try {
  await server.listen()
  await mkdir(shots, { recursive: true })
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH
      ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] }
      : process.env.CC2_BROWSER_EXECUTABLE
        ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE }
        : {}),
  })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() !== 'GET' && !r.url().includes('/__preview/')) writes.push(`${r.method()} ${new URL(r.url()).pathname}`)
  })
  const now = Math.floor(Date.now() / 1000)
  // What the plate library answers: the demo's own library with the dates and plates of the case.
  let library = lib => lib
  let history = []
  let mounted = null
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const reply = await route.fetch()
    const lib = await reply.json()
    for (const p of lib.plates) for (const m of p.measures) m.measured = now - 3600
    mounted = lib.plates.find(p => p.id === lib.current) || null
    return route.fulfill({ json: library(lib) })
  })
  await page.route('**/api/screws/history', route =>
    route.request().method() === 'GET' ? route.fulfill({ json: { sessions: history } }) : route.fallback()
  )
  const measurement = (z, time = now - 120) => [
    { kind: 'session', started: time - 60, ended: time, measurements: [{ time, kind: 'check', corner: null, twice: false, z }] },
  ]
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  const card = page.locator('#meshAlign')
  const part = name => card.locator(`[data-align="${name}"]`)
  const open = async () => {
    await page.reload()
    await page.locator('#cc2-preview-banner').waitFor()
    await page.locator('#cc2-navigation a[href="#bed"]').click()
    await page.getByRole('tab', { name: 'Mesh alignment', exact: true }).click()
    await card.getByRole('heading', { name: 'Quick check with four probes', exact: true }).waitFor()
  }
  const state = value => card.locator(`[data-align="quick"][data-align-quick="${value}"]`).waitFor()

  // No four-point measurement yet: the section says so and shows no numbers.
  await open()
  await state('waiting')
  await part('quick').getByText('There is no four-point measurement yet. Run one on the Screw Levelling tab.').waitFor()
  assert.ok(mounted, 'the demo has a mounted plate')
  const reference = mounted.measures.filter(m => m.temp === 60)[0]
  assert.ok(reference, 'the mounted demo plate has a 60 °C mesh')

  // Probes that carry the plate shape plus a known tilt: the shape is taken away, the tilt remains.
  const probes = (tiltX, tiltY, extra = {}) => {
    const z = {}
    for (const s of ALIGN_DEFAULTS.screws)
      z[s.id] =
        meshAt(reference.mesh, s.x, s.y) +
        (tiltX * s.x) / ALIGN_DEFAULTS.tilt_span_mm +
        (tiltY * s.y) / ALIGN_DEFAULTS.tilt_span_mm +
        (extra[s.id] || 0)
    return z
  }
  let z = probes(0.2, -0.12)
  let expected = analyseScrewProbes(z, reference.mesh)
  history = measurement(z)
  await open()
  await state('result')
  await part('quick-tilt-x').getByText(`${signed(expected.tilt_x)} mm`).waitFor()
  await part('quick-tilt-y').getByText(`${signed(expected.tilt_y)} mm`).waitFor()
  assert.equal(await part('quick-verdict').getAttribute('data-aligned'), 'no')
  assert.equal(await part('quick-screws').locator('[data-align-screw]').count(), 4)
  await part('quick-screws')
    .locator(`[data-align-screw="${expected.reference}"]`)
    .getByText("Reference screw: leave it as it is", { exact: true })
    .waitFor()
  await part('quick-basis').getByText(mounted.name, { exact: false }).waitFor()
  assert.equal(await part('quick-stale').count(), 0)
  assert.equal(await part('quick-twist').count(), 0)
  await page.screenshot({ path: path.join(shots, 'mesh-align-quick-1440.png'), fullPage: true })

  // The same tilt the full mesh shows is what the quick check has to show for the plain plate shape.
  history = measurement(probes(0, 0))
  expected = analyseScrewProbes(probes(0, 0), reference.mesh)
  await open()
  await state('result')
  assert.equal(await part('quick-verdict').getAttribute('data-aligned'), expected.aligned ? 'yes' : 'no')

  // Old probes and a twist are warned about; the numbers stay visible.
  z = probes(0.2, -0.12, { FL: 0.06, RR: 0.06 })
  assert.equal(analyseScrewProbes(z, reference.mesh).twist_warning, true)
  history = measurement(z, now - 3 * 3600)
  await open()
  await state('result')
  await part('quick-stale').getByText('Measure again after turning a screw.', { exact: false }).waitFor()
  await part('quick-twist').getByText('Measure a full mesh.', { exact: false }).waitFor()

  // Refusals: an old full mesh, no full mesh at the probe temperature, no mounted plate.
  const refused = async (change, reason, text) => {
    library = change
    await open()
    await state('refused')
    assert.equal(await part('quick-refusal').getAttribute('data-align-reason'), reason)
    await part('quick-refusal').getByText(text, { exact: false }).waitFor()
    assert.equal(await part('quick-screws').count(), 0, `${reason}: no corrections are shown`)
  }
  history = measurement(probes(0.2, -0.12))
  const own = (lib, edit) => ({ ...lib, plates: lib.plates.map(p => (p.id === lib.current ? edit(p) : p)) })
  await refused(
    lib => own(lib, p => ({ ...p, measures: p.measures.map(m => ({ ...m, measured: now - 40 * 86400 })) })),
    'old',
    'is 40 days old'
  )
  await refused(
    lib => own(lib, p => ({ ...p, measures: p.measures.filter(m => m.temp !== 60) })),
    'no_reference',
    'has no saved full mesh at 60 °C'
  )
  await refused(lib => ({ ...lib, current: '' }), 'no_plate', 'No plate is mounted in the plate library')

  assert.deepEqual(writes, [], 'the quick check sends nothing')
  assert.deepEqual(errors, [])
  console.log('PASS mesh-guided screw alignment quick check: plate shape taken away, tilt and screws, stale and twist warnings, refusals for old, missing and unmounted reference, read-only')
} finally {
  await browser?.close()
  await server.close()
}
