// Bed alignment tab, step 2 with "Quick" and the tick "With mesh": four probes of the screws less the plate shape of the
// saved full mesh chosen in step 1. The table gives measured, plate shape relative to the front-left screw (the
// picture does the same); without the tick the mesh is dropped again; a twist and a mesh of another temperature are
// warned about; nothing is sent to the printer. All data is synthetic.
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS, analyseMesh, analyseScrewProbes, meshAt } from '../src/lib/mesh-align.ts'
import { turnPlan } from '../src/lib/screws.ts'
import { dome, syntheticMesh } from './mesh-align-synthetic.mjs'

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
  let tweak = lib => lib
  let screwTemp = '60'
  let choice = ''
  await page.addInitScript(() => {
    // the choice is written by the test below through localStorage before each load
  })
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const reply = await route.fetch()
    const lib = await reply.json()
    for (const p of lib.plates) for (const m of p.measures) m.measured = now - 3600
    return route.fulfill({ json: tweak(lib) })
  })
  const output = z =>
    `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${Object.entries(z)
      .map(([k, v]) => {
        const s = ALIGN_DEFAULTS.screws.find(x => x.id === k)
        return `${`// probe at ${Number(s.x).toFixed(3)},${Number(s.y).toFixed(3)} is z=${v.toFixed(6)}\n`.repeat(3)}// Result is z=${v.toFixed(6)}\n`
      })
      .join('')}`
  let job = null
  await page.route('**/api/console', route =>
    route.request().method() === 'GET' && job
      ? route.fulfill({ json: { busy: false, completed: true, success: true, generation: 80, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: job } })
      : route.fallback()
  )
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const library = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const mounted = library.plates.find(p => p.id === library.current)
  assert.ok(mounted, 'the demo has a mounted plate')
  const reference = mounted.measures.filter(m => m.temp === 60)[0]
  assert.ok(reference, 'the mounted demo plate has a 60 °C mesh')
  const card = page.locator('#screwFocus')
  // The tick "With mesh" beside the two ways of measuring (kept in this browser).
  const tick = async on => {
    const box = page.locator('[data-level-mesh-toggle] input')
    if ((await box.isChecked()) !== on) await box.click()
    assert.equal(await box.isChecked(), on)
  }
  const open = async () => {
    await page.evaluate(([c, t]) => localStorage.setItem('cc2-level-base', JSON.stringify({ choice: c, temp: t })), [`measure:${reference.id}`, screwTemp])
    await page.goto(`${origin}/#bed/align`)
    await page.reload()
    await page.locator('#cc2-preview-banner').waitFor()
    await card.waitFor()
    const b = page.locator('[data-level-step="2"] > button')
    if ((await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    await page.locator('[data-screw-measured="FL"]').waitFor({ timeout: 10000 })
    // The plate shape is part of the evaluation with the tick "With mesh" of step 1 (the way stays "Quick").
    await tick(true)
    assert.equal(await page.locator('[data-level-start-mode-button="quick"]').getAttribute('aria-pressed'), 'true')
  }
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
  const um = mm => Math.sign(mm) * Math.floor(Math.abs(mm) * 1000 + 0.5)
  const read = async attr => {
    const out = {}
    for (const id of ['FL', 'FR', 'RR', 'RL']) {
      // Measured: the table shows the Z height as measured; the difference to front left is computed from it.
      const loc = attr === 'measured' ? `[data-screw-zheight="${id}"]` : `[data-screw-${attr}="${id}"]`
      const t = (await page.locator(loc).innerText()).replace(/\u00a0/g, ' ').replace('Z height: ', '').replace('−', '-').replace(/ µm$/, '')
      out[id] = Number(t)
    }
    if (attr === 'measured') for (const id of ['RL', 'RR', 'FR', 'FL']) out[id] -= out.FL
    return out
  }
  const near = (got, want, what) => {
    for (const id of Object.keys(want)) assert.ok(Math.abs(got[id] - want[id]) <= 1, `${what} ${id}: ${got[id]} vs ${want[id]}`)
  }

  // Probes with the shape and a known tilt: tilt in the line, every column relative to the front-left screw.
  let z = probes(0.2, -0.12)
  const expected = analyseScrewProbes(z, reference.mesh)
  job = output(z)
  await open()
  // The tilt stands in two tiles (X and Y), no longer in a line of text.
  await page.locator('[data-align="tilt-x"] strong').getByText(`${signed(expected.tilt_x)} mm`, { exact: true }).waitFor()
  await page.locator('[data-align="tilt-y"] strong').getByText(`${signed(expected.tilt_y)} mm`, { exact: true }).waitFor()
  assert.equal(await page.locator('[data-level-mesh-line]').count(), 0, 'no text line for the tilt any more')
  const ids = ['FL', 'FR', 'RR', 'RL']
  const rel = (list, f) => Object.fromEntries(ids.map((id, i) => [id, um(f(i) - f(0))]))
  near(await read('measured'), rel(ids, i => z[ids[i]]), 'measured')
  near(await read('shape'), rel(ids, i => expected.shape[i]), 'plate shape')
  assert.equal((await page.locator('[data-screw-measured="FL"]').innerText()).replace(/\u00a0/g, ' '), 'Reference', 'front left is the reference')
  assert.equal((await page.locator('[data-screw-shape="FL"]').innerText()).replace(/\u00a0/g, ' '), '0 µm')
  assert.equal(await page.locator('[data-screw-target]').count(), 0, 'there is no Target column any more')
  // Front left shows the real height as the reference; what is left to turn is measured minus plate shape.
  const zh = async id => (await page.locator(`[data-screw-zheight="${id}"]`).innerText()).replace(/\u00a0/g, ' ')
  const zText = id => `${um(z[id]) < 0 ? '−' : ''}${Math.abs(um(z[id]))} µm`
  assert.equal(await zh('FL'), zText('FL'), 'the measured raw value, also with the mesh')
  assert.equal(await zh('FR'), zText('FR'))
  // With a mesh the status is the plan after the plate shape: a small line under the table says so (standard plan).
  assert.equal(await page.locator('[data-screw-mesh-note]').count(), 1, 'one line explains the status with a mesh')
  assert.equal((await page.locator('[data-screw-mesh-note]').innerText()).trim(), 'The status follows the plan: the measured deviation minus the plate shape of the mesh.')
  for (const id of ['FR', 'RR', 'RL']) {
    const left = rel(ids, i => z[ids[i]])[id] - rel(ids, i => expected.shape[i])[id]
    if (Math.abs(left) > 22) {
      // Auto keeps the rest visible: what remains after the whole clicks, against the target cleaned by the mesh.
      const i = ids.indexOf(id)
      const exactLeft = (z[id] - z.FL - (expected.shape[i] - expected.shape[0])) * 1000
      const plan = turnPlan(-exactLeft, 700)
      const rest = um(plan.rest / 1000)
      const want = `${plan.fine.deg}° · remaining ${rest > 0 ? '+' : rest < 0 ? '−' : ''}${Math.abs(rest)} µm`
      assert.equal((await page.locator(`[data-screw-aid="${id}"]`).innerText()).replace(/\u00a0/g, ' '), want, `Auto with mesh: rest of ${id}`)
    }
    if (Math.abs(left) > 22) assert.ok(Math.abs(Number((await page.locator(`[data-screw-amount="${id}"]`).innerText()).replace(/\u00a0/g, ' ').replace(' µm', '')) - Math.abs(left)) <= 1, `amount of ${id}`)
  }
  assert.equal(await page.locator('[data-level-mesh-warning]').count(), 0, 'no warning for a fitting mesh at the same temperature')
  await page.screenshot({ path: path.join(shots, 'mesh-align-quick-1440.png'), fullPage: true })

  // Without the tick the mesh is dropped: no plate-shape column, no tilt line, the target is the front-left height again (0).
  await tick(false)
  assert.equal(await page.locator('[data-screw-shape]').count(), 0)
  assert.equal(await page.locator('[data-level-mesh-tiles]').count(), 0, 'no tilt tiles without the mesh')
  near(await read('measured'), rel(ids, i => z[ids[i]]), 'measured (4 points only)')
  await tick(true)
  await page.locator('[data-screw-shape="FR"]').waitFor()

  // A plate bowed beyond the limit is named with both numbers; it is a note, the table stays.
  // The own limit only counts with the experimental settings switched on.
  await page.route('**/api/experiments', route =>
    route.fulfill({
      json: { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, mesh_align_settings: true },
    })
  )
  await page.evaluate(() => localStorage.setItem('cc2-mesh-align', JSON.stringify({ shape_warn_mm: 0.01 })))
  await open()
  const bowed = page.locator('[data-level-mesh-warning]').filter({ hasText: 'The plate is bowed by' })
  await bowed.waitFor()
  assert.match((await bowed.innerText()).replace(/\u00a0/g, ' '), /limit 0\.01 mm/)
  // Every (i) of the settings block opens its text inside the card, also in the right column (at 1440 px four columns,
  // at 800 px two).
  const settingsBlock = page.locator('details[data-align="settings"]')
  await settingsBlock.locator('summary').click()
  for (const width of [1440, 800]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(300)
    const hints = settingsBlock.locator('[data-align-hint] > button')
    for (let i = 0; i < (await hints.count()); i++) {
      await hints.nth(i).scrollIntoViewIfNeeded()
      await hints.nth(i).hover()
      const tip = settingsBlock.locator('[data-hint-text]')
      await tip.waitFor()
      const box = await tip.boundingBox()
      const cardNow = await page.locator('#screwFocus').boundingBox()
      assert.ok(box.x >= cardNow.x - 0.5 && box.x + box.width <= cardNow.x + cardNow.width + 0.5, `${width} px: the text of (i) ${i + 1} stays inside the card (${Math.round(box.x)}–${Math.round(box.x + box.width)} / ${Math.round(cardNow.x)}–${Math.round(cardNow.x + cardNow.width)})`)
      await page.mouse.move(5, 5)
      await tip.waitFor({ state: 'detached' })
    }
  }
  await page.setViewportSize({ width: 1440, height: 1000 })
  await settingsBlock.locator('summary').click()
  await page.locator('[data-screw-shape="FR"]').waitFor()
  await page.evaluate(() => localStorage.removeItem('cc2-mesh-align'))
  await page.unroute('**/api/experiments')
  await open()
  assert.equal(await page.locator('[data-level-mesh-warning]').count(), 0, 'within the limit: no shape warning')

  // A twist is warned about; the numbers stay visible.
  z = probes(0.2, -0.12, { FL: 0.06, RR: 0.06 })
  assert.equal(analyseScrewProbes(z, reference.mesh).twist_warning, true)
  job = output(z)
  await open()
  await page.locator('[data-level-mesh-warning]').getByText('Measure a full mesh.', { exact: false }).waitFor()

  // The screws were measured at another bed temperature than the mesh: the warning names both.
  job = output(probes(0.2, -0.12))
  screwTemp = '70'
  await open()
  await page.locator('[data-level-mesh-warning]').getByText(`The mesh was measured at ${reference.temp} °C, the screws at 70 °C.`, { exact: false }).waitFor()

  // The plate shape is named under the plate map (the fourth view of the top view).
  const shapeOf = async (grid, name) => {
    tweak = lib => {
      for (const p of lib.plates) for (const m of p.measures) if (m.id === reference.id) m.mesh = grid
      return lib
    }
    job = output(probes(0, 0))
    await open()
    // The shape is named in the fourth view (the plate map): switch there with the view button.
    for (let i = 0; i < 3; i++) await page.locator('[data-level-view-toggle]').click()
    await page.locator('[data-level-mesh-map]').getByText(name, { exact: false }).waitFor()
  }
  const domeGrid = syntheticMesh({ tiltX: 0.03, tiltY: -0.02, shape: dome(0.5) })
  assert.equal(analyseMesh(domeGrid).shape, 'dome')
  await shapeOf(domeGrid, 'Dome, centre high')
  const planeGrid = syntheticMesh({ tiltX: 0.2, tiltY: -0.1 })
  assert.equal(analyseMesh(planeGrid).shape, 'flat')
  await shapeOf(planeGrid, 'Nearly flat')
  tweak = lib => lib

  // The mesh of another plate than the mounted one, and of another nozzle, are warned about.
  const owner = library.plates.find(p => p.measures.some(m => m.id === reference.id))
  const mountedOther = library.plates.find(p => p.id !== owner.id)
  tweak = lib => ({ ...lib, current: mountedOther.id })
  job = output(probes(0.2, -0.12))
  await open()
  await page.locator('[data-level-mesh-warning]').getByText(`The mesh belongs to plate “${owner.name}”, the mounted plate is “${mountedOther.name}”.`).waitFor()
  const otherNozzle = library.nozzles.find(n => n.id !== reference.nozzle)
  assert.ok(otherNozzle && reference.nozzle, 'the demo has two nozzles')
  tweak = lib => ({ ...lib, nozzle: otherNozzle.id })
  await open()
  await page.locator('[data-level-mesh-warning]').getByText('The mesh was measured with nozzle', { exact: false }).waitFor()
  tweak = lib => lib

  // Step 1 judges the age of the mesh (was the "old" refusal of the old quick card): usable, then over the limit.
  job = output(probes(0.2, -0.12))
  await open()
  await page.locator('[data-level-age]').getByText('Mesh usable as the basis (limit 30 days / 10 prints)').waitFor()
  tweak = lib => {
    for (const p of lib.plates) for (const m of p.measures) m.measured = now - 40 * 86400
    return lib
  }
  await open()
  // Over the limit: no box above the choice any more; the mesh can still be used, and the note under the starts says how
  // old it is and recommends a new mesh.
  await tick(true)
  await page.locator('[data-level-saved-mesh-aged]').getByText('Mesh too old (40 days / ', { exact: false }).waitFor()
  await page.locator('[data-level-saved-mesh-aged]').getByText('Recommended: “Measure a new mesh”.', { exact: false }).waitFor()
  assert.equal(await page.locator('[data-level-age]').count(), 0, 'no age box for a mesh over the limit')
  const startQuick = page.locator('[data-level-start-button="quick"]')
  assert.equal(await startQuick.isDisabled(), false, 'an old mesh can still be used')
  const startBox = await startQuick.boundingBox()
  const aged = await page.locator('[data-level-saved-mesh-aged]').boundingBox()
  assert.ok(aged.y >= startBox.y + startBox.height - 1 && Math.abs(aged.x - startBox.x) < 2, 'the note stands under the starts, in their column')
  tweak = lib => lib

  assert.deepEqual(writes, [], 'the evaluation sends nothing')
  assert.deepEqual(errors, [])
  console.log('PASS bed alignment step 2 with mesh: values relative to front left, plate shape and target, switch, twist and temperature warnings, read-only')
} finally {
  await browser?.close()
  await server.close()
}
