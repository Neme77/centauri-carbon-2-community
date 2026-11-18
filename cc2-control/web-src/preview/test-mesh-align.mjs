// Mesh-guided screw alignment: the analysis maths (imported below) and, on the Bed alignment tab, the note on
// the CC2 defaults, the experimental opt-in with its settings block (no thread controls: the screw is the CC2's
// own M4) and the German title. The old stand-alone analysis page (tilt tiles, heat map, verdict) is gone; the
// tilt and the plate shape appear in step 3 and are checked in test-mesh-align-quick.mjs.
import './test-mesh-align-math.mjs'
import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS, analyseMesh } from '../src/lib/mesh-align.ts'
import { dome, syntheticMesh } from './mesh-align-synthetic.mjs'

// What the old page showed as verdict, shape warning, plane colour and screw corrections is still worked out by
// the library (the guided loop and step 3 use it); the numbers are checked here without a page.
const domed = analyseMesh(syntheticMesh({ tiltX: 0.03, tiltY: -0.02, shape: dome(0.5) }))
assert.equal(domed.aligned, true, 'a small tilt under a dome: aligned')
assert.equal(domed.shape_warning, true, 'a dome of 0.5 mm: shape warning')
assert.equal(domed.shape, 'dome')
assert.notEqual(domed.raw_range, domed.residual_range, 'the shape range is not the raw range')
const plane = analyseMesh(syntheticMesh({ tiltX: 0.223, tiltY: -0.223 }))
assert.ok(plane.residual_range < 0.0005, 'an exact plane has no shape')
assert.equal(plane.aligned, false)
const tilted = analyseMesh(syntheticMesh({ tiltX: 0, tiltY: 0.96, shape: dome(0.2) }))
const rear = tilted.screws.find(s => s.id === 'RR').correction
assert.ok(rear < 0 && Math.abs(rear) < Math.abs(tilted.tilt_y), 'tilt and screw correction are different numbers')
assert.equal(tilted.aligned, false)
const raised = analyseMesh(syntheticMesh({ tiltX: -0.6 }))
assert.ok(raised.screws.some(s => s.correction > 0.4), 'the low side gets a positive correction')
assert.equal(raised.screws.find(s => s.reference).correction, 0, 'the reference screw is not turned')
// The settings change the verdict, the shape warning and the tilt span.
assert.equal(analyseMesh(syntheticMesh({ tiltX: 0, tiltY: 0.96, shape: dome(0.2) }), { ...ALIGN_DEFAULTS, target_tilt_mm: 1 }).aligned, true)
assert.equal(analyseMesh(syntheticMesh({ shape: dome(0.2) }), { ...ALIGN_DEFAULTS, shape_warn_mm: 0.05 }).shape_warning, true)
assert.equal(analyseMesh(syntheticMesh({ shape: dome(0.2) })).shape_warning, false)
assert.ok(Math.abs(analyseMesh(syntheticMesh({ tiltY: 0.96 }), { ...ALIGN_DEFAULTS, tilt_span_mm: 120 }).tilt_y - analyseMesh(syntheticMesh({ tiltY: 0.96 })).tilt_y / 2) < 1e-6, 'half the span, half the tilt')

const root = fileURLToPath(new URL('..', import.meta.url))
const german = JSON.parse(await readFile(path.join(root, 'public/locales/de.json'), 'utf8'))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
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
  // The display is read-only: nothing but GET may leave the page, except the demo's own scene switch.
  page.on('request', r => {
    if (r.method() !== 'GET' && !r.url().includes('/__preview/')) writes.push(`${r.method()} ${new URL(r.url()).pathname}`)
  })
  // The settings block is an experimental opt-in; the test starts without it.
  let settingsOptIn = false
  await page.route('**/api/experiments', route =>
    route.fulfill({
      json: { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, mesh_align_settings: settingsOptIn },
    })
  )
  // The demo serves only some locales; German comes straight from the locale file.
  await page.route('**/i18n/de.json', route => route.fulfill({ json: german }))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const card = page.locator('#screwFocus')
  const part = name => card.locator(`[data-align="${name}"]`)
  const reveal = async () => {
    await page.goto(`${origin}/#bed/align`)
    await card.waitFor()
    for (const n of [2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
  }
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Bed alignment', exact: true }).click()
  await card.getByRole('heading', { name: 'Bed alignment', exact: true }).waitFor()
  await reveal()

  // Without the opt-in the page says which defaults it uses and where to change them; no settings block.
  assert.equal(await part('settings').count(), 0)
  await part('settings-off').getByText('This page uses the CC2 defaults').waitFor()
  await part('settings-off').getByRole('button', { name: 'Open experimental integrations', exact: true }).click()
  await page.waitForFunction(() => location.hash === '#settings/integrations')
  await page.getByRole('heading', { name: 'Screw pitch and alignment settings', exact: true }).waitFor()
  settingsOptIn = true
  await page.reload()
  await reveal()
  const settings = part('settings')
  await settings.waitFor()
  assert.equal(await part('settings-off').count(), 0)
  await settings.getByText('Screw pitch and alignment settings', { exact: true }).click()
  await settings.getByText('Kept in this browser. The defaults are the CC2 standard.', { exact: true }).waitFor()
  // No word about where a default comes from; instead every field says behind an (i) where it takes effect.
  assert.equal(await settings.getByText(/estimate/i).count(), 0, 'no "estimate" in the settings block')
  assert.equal(await settings.locator('[data-align-hint]').count(), 6, 'four number fields, the screw positions and the row order carry a hint')
  const field = name => settings.getByLabel(name, { exact: true })
  // The thread is the CC2 screw and cannot be changed: no controls for pitch, direction, their sources or the wiki.
  for (const label of [/^Klipper thread type/, /^Thread pitch/, /^Source of the thread pitch/, /^Turning direction/, /^Source of the turning direction/])
    assert.equal(await settings.getByLabel(label).count(), 0, `no control for ${label}`)
  assert.equal(await settings.getByRole('button', { name: 'Use the Elegoo wiki statement', exact: true }).count(), 0)
  assert.equal(await settings.getByText(/Elegoo wiki/).count(), 0)

  // The fields of the retired loop (target tilt, number of measurements) are gone.
  for (const label of [/^Target tilt/, /^Most measurements/, /measurements/i])
    assert.equal(await settings.getByLabel(label).count(), 0, `no control for ${label}`)

  // The settings survive a reload and go back to the CC2 defaults.
  assert.equal(Number(await field('Shape warning above (mm)').inputValue()), 0.3)
  await field('Shape warning above (mm)').fill('0.01')
  await field('Shape warning above (mm)').blur()
  await field('Tilt span (mm)').fill('120')
  await field('Tilt span (mm)').blur()
  await page.reload()
  await reveal()
  await settings.getByText('Screw pitch and alignment settings', { exact: true }).click()
  assert.equal(Number(await field('Shape warning above (mm)').inputValue()), 0.01)
  assert.equal(Number(await field('Tilt span (mm)').inputValue()), 120)
  await settings.getByRole('button', { name: 'Reset to the CC2 defaults', exact: true }).click()
  assert.equal(Number(await field('Shape warning above (mm)').inputValue()), 0.3)
  assert.equal(Number(await field('Tilt span (mm)').inputValue()), 240)

  // Phone width: nothing overflows.
  await page.setViewportSize({ width: 390, height: 844 })
  await page.reload()
  await reveal()
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'mobile must not overflow')
  await page.setViewportSize({ width: 1440, height: 1000 })
  assert.deepEqual(writes, [], 'the page sends nothing to the printer')

  // German: the tab and its title are translated.
  await page.evaluate(() => {
    location.hash = '#settings/appearance'
  })
  await page.locator('main select:has(option[value="de"])').selectOption('de')
  await page.waitForFunction(() => document.documentElement.lang === 'de')
  assert.deepEqual(writes, ['PUT /api/preferences'], 'only the language choice was saved')
  await page.evaluate(() => {
    location.hash = '#bed/align'
  })
  await card.getByRole('heading', { name: german['bed.level_title'], exact: true }).waitFor()
  assert.deepEqual(errors, [])
  console.log('PASS mesh-guided screw alignment maths and the Bed alignment tab: CC2 defaults note, experimental settings, German title')
} finally {
  await browser?.close()
  await server.close()
}
