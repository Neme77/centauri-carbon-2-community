// Mesh-guided screw alignment, the display: tilt and screw corrections shown apart, the plate-shape heat
// map, the verdict and the shape warning, turns and direction only with a configured source. Every mesh
// is synthetic; the expected numbers come from the analysis, not from a printer.
import './test-mesh-align-math.mjs'
import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { analyseMesh } from '../src/lib/mesh-align.ts'
import { asProfileReply, dome, syntheticMesh } from './mesh-align-synthetic.mjs'

const root = fileURLToPath(new URL('..', import.meta.url))
const german = JSON.parse(await readFile(path.join(root, 'public/locales/de.json'), 'utf8'))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
const number = (language, value, digits) =>
  new Intl.NumberFormat(language, { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(Math.abs(value))
const signed = (language, value, digits = 3) => `${value > 0 ? '+' : value < 0 ? '−' : ''}${number(language, value, digits)}`
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
  let mesh = null
  // The settings block is an experimental opt-in; the test starts without it.
  let settingsOptIn = false
  await page.route('**/api/experiments', route =>
    route.fulfill({
      json: { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, mesh_align_settings: settingsOptIn },
    })
  )
  await page.route('**/api/mesh', route => (mesh ? route.fulfill({ json: mesh }) : route.fallback()))
  // The demo serves only some locales; German comes straight from the locale file.
  await page.route('**/i18n/de.json', route => route.fulfill({ json: german }))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Mesh alignment', exact: true }).click()
  const card = page.locator('#meshAlign')
  await card.getByRole('heading', { name: 'Mesh-guided screw alignment', exact: true }).waitFor()
  const part = name => card.locator(`[data-align="${name}"]`)
  const screw = id => card.locator(`[data-align-screw="${id}"]`)
  const reload = card.getByRole('button', { name: 'Reload meshes', exact: true })
  const source = part('source')
  const show = async (reply, expectedBasis) => {
    mesh = reply
    const answered = page.waitForResponse(r => r.url().endsWith('/api/mesh'))
    await reload.click()
    await answered
    if (expectedBasis) await part('basis').getByText(expectedBasis).waitFor()
  }

  // The demo's own active mesh: a pure tilt in X and Y, analysed without any request but GET.
  await part('basis').getByText('Plane through 121 mesh points').waitFor()
  assert.equal(await source.inputValue(), 'active')
  await part('tilt-x').getByText('right side higher · over 240 mm').waitFor()
  await part('tilt-y').getByText('front higher · over 240 mm').waitFor()
  await part('verdict').getByText('Not aligned yet').waitFor()
  assert.equal(await part('verdict').getAttribute('data-aligned'), 'no')
  await part('tag').getByText('Not aligned', { exact: true }).waitFor()
  await part('shape').getByText('Nearly flat', { exact: true }).waitFor()
  assert.equal(await part('shape-warning').count(), 0)
  await card.getByText('Target: both tilts below 0.050 mm. This limit is an estimate').waitFor()
  // The alignment has its own tab; Screw Levelling keeps the four-point card, unchanged, without it.
  await page.getByRole('tab', { name: 'Screw Levelling', exact: true }).click()
  await page.locator('#screwFocus').getByRole('button', { name: /Start session/ }).waitFor()
  assert.equal(await page.locator('#meshAlign').count(), 0)
  await page.getByRole('tab', { name: 'Mesh alignment', exact: true }).click()
  await card.getByRole('heading', { name: 'Mesh-guided screw alignment', exact: true }).waitFor()
  assert.equal(await page.locator('#screwFocus').count(), 0)

  // A dome under a small tilt: the alignment is done, the rest is plate shape, and the warning stays.
  const domeGrid = syntheticMesh({ tiltX: 0.03, tiltY: -0.02, shape: dome(0.5) })
  const domed = analyseMesh(domeGrid)
  assert.equal(domed.aligned, true)
  assert.equal(domed.shape_warning, true)
  await show(asProfileReply('default', domeGrid))
  await part('verdict')
    .getByText(
      `Alignment done, the rest is plate shape: Dome, centre high, range ${number('en', domed.residual_range, 3)} mm.`
    )
    .waitFor()
  assert.equal(await part('verdict').getAttribute('data-aligned'), 'yes')
  assert.equal(await source.inputValue(), 'default', 'without an active mesh the side mesh is analysed')
  await part('tag').getByText('Aligned', { exact: true }).waitFor()
  await part('tilt-x').getByText(`${signed('en', domed.tilt_x)} mm`).waitFor()
  await part('tilt-y').getByText(`${signed('en', domed.tilt_y)} mm`).waitFor()
  await part('tilt-y').getByText('front higher').waitFor()
  await part('shape').getByText('Dome, centre high', { exact: true }).waitFor()
  await part('shape')
    .getByText(`Range ${number('en', domed.residual_range, 3)} mm · RMS ${number('en', domed.residual_rms, 3)} mm`)
    .waitFor()
  await part('shape-warning').getByText('more than the warning limit of 0.30 mm').waitFor()
  await part('shape-warning').getByText('This is a warning only').waitFor()
  // The verdict names the range of the shape, not the range of the raw mesh.
  await card.getByText(`Range of the mesh with its tilt: ${number('en', domed.raw_range, 3)} mm.`).waitFor()
  assert.notEqual(number('en', domed.raw_range, 3), number('en', domed.residual_range, 3))
  const map = part('heatmap')
  assert.equal(await map.locator('svg rect').count(), 121, 'one cell per mesh point')
  assert.equal(await map.locator('svg circle').count(), 4, 'the four screws are marked')
  assert.match(await map.locator('svg').getAttribute('aria-label'), /lowest point −0\.\d{3} mm, highest point \+0\.\d{3} mm/)
  const fills = await map.locator('svg rect').evaluateAll(cells => cells.map(c => c.getAttribute('fill')))
  // A dome: the centre is high (red), the corners are low (blue). Row 0 of the mesh is the front.
  const rgb = fill => fill.match(/\d+/g).map(Number)
  const centre = rgb(fills[60]),
    corner = rgb(fills[0])
  assert.ok(centre[0] > centre[2], `the centre is high: ${fills[60]}`)
  assert.ok(corner[2] > corner[0], `the corner is low: ${fills[0]}`)
  await page.screenshot({ path: path.join(shots, 'mesh-align-done-1440.png'), fullPage: true })

  // A bed that rises to the back: tilt and screw correction are different numbers, shown apart.
  const tiltedGrid = syntheticMesh({ tiltX: 0, tiltY: 0.96, shape: dome(0.2) })
  const tilted = analyseMesh(tiltedGrid)
  const rear = tilted.screws.find(s => s.id === 'RR').correction
  assert.ok(rear < 0 && Math.abs(rear) < Math.abs(tilted.tilt_y))
  await show(asProfileReply('default', tiltedGrid))
  await part('tilt-y').getByText(`${signed('en', tilted.tilt_y)} mm`).waitFor()
  await part('tilt-y').getByText('back higher · over 240 mm').waitFor()
  await part('tilt-x').getByText('level · over 240 mm').waitFor()
  await screw('FL').getByText('Reference screw: leave it as it is', { exact: true }).waitFor()
  await screw('FR').getByText('Nothing to change here', { exact: true }).waitFor()
  const lower = `Lower the bed at this corner by ${number('en', rear, 2)} mm`
  await screw('RR').getByText(lower, { exact: true }).waitFor()
  await screw('RL').getByText(lower, { exact: true }).waitFor()
  await part('verdict').getByText(`Not aligned yet: tilt X is ${number('en', 0, 3)} mm and tilt Y is ${signed('en', tilted.tilt_y)} mm.`).waitFor()
  await part('verdict').getByText('Both must be below 0.050 mm.').waitFor()
  assert.equal(await part('shape-warning').count(), 0)
  // Without configured sources: millimetres only, and the page says why.
  await card.getByText('Turns are not shown: no thread pitch with a source is set.').waitFor()
  await card.getByText('The turning direction is not shown: no source for it is set.').waitFor()
  assert.equal(await part('screws').getByText(/turns|clockwise/).count(), 0)

  // A bed that is low on the right is raised there.
  const lowRight = syntheticMesh({ tiltX: -0.6 })
  const raised = analyseMesh(lowRight)
  await show(asProfileReply('default', lowRight))
  await part('tilt-x').getByText('left side higher · over 240 mm').waitFor()
  for (const s of raised.screws) {
    const text = s.reference
      ? 'Reference screw: leave it as it is'
      : Math.abs(s.correction) < 0.005
        ? 'Nothing to change here'
        : `${s.correction > 0 ? 'Raise' : 'Lower'} the bed at this corner by ${number('en', s.correction, 2)} mm`
    await screw(s.id).getByText(text, { exact: true }).waitFor()
  }
  assert.ok(raised.screws.some(s => s.correction > 0.4), 'the low side gets a positive correction')
  await show(asProfileReply('default', tiltedGrid))
  await screw('RR').getByText(lower, { exact: true }).waitFor()

  // Turns appear once a thread pitch and its source are set; the direction needs its own source.
  // Without the opt-in the page says which defaults it uses and where to change them; no settings block.
  assert.equal(await part('settings').count(), 0)
  await part('settings-off').getByText('This page uses the CC2 defaults').waitFor()
  await part('settings-off').getByRole('button', { name: 'Open experimental integrations', exact: true }).click()
  await page.waitForFunction(() => location.hash === '#settings/integrations')
  await page.getByRole('heading', { name: 'Mesh alignment settings', exact: true }).waitFor()
  settingsOptIn = true
  await page.evaluate(() => {
    location.hash = '#bed/align'
  })
  await card.getByRole('heading', { name: 'Mesh-guided screw alignment', exact: true }).waitFor()
  const settings = part('settings')
  await settings.waitFor()
  assert.equal(await part('settings-off').count(), 0)
  await settings.getByText('Alignment settings', { exact: true }).click()
  await settings.getByText('Target, rounds and warning limit are estimates, not measured values.').waitFor()
  const field = name => settings.getByLabel(name, { exact: true })
  const rotation = settings.getByLabel(/^Turning direction/)
  await field('Thread pitch (mm per turn)').fill('0.5')
  await field('Thread pitch (mm per turn)').blur()
  assert.equal(await part('screws').getByText(/turns/).count(), 0, 'a pitch without a source shows no turns')
  await field('Source of the thread pitch').fill('measured on the screw')
  await field('Source of the thread pitch').blur()
  const turns = `about ${number('en', Math.abs(rear) / 0.5, 2)} turns`
  await screw('RR').getByText(`${lower} (${turns})`, { exact: true }).waitFor()
  assert.equal(await card.getByText('Turns are not shown').count(), 0)
  assert.equal(await part('screws').getByText(/clockwise/).count(), 0, 'turns alone show no direction')
  await rotation.selectOption('raises')
  assert.equal(await part('screws').getByText(/clockwise/).count(), 0, 'a direction without a source is not shown')
  await field('Source of the turning direction').fill('own test')
  await field('Source of the turning direction').blur()
  await screw('RR').getByText(`${lower} (${turns}, counter-clockwise)`, { exact: true }).waitFor()
  // The wiki sentence is used only when it is chosen, and then as "clockwise lowers the bed".
  await settings.getByText('It is used only when it is chosen here').waitFor()
  await settings.getByRole('button', { name: 'Use the Elegoo wiki statement', exact: true }).click()
  await screw('RR').getByText(`${lower} (${turns}, clockwise)`, { exact: true }).waitFor()
  assert.equal(await field('Source of the turning direction').inputValue(), 'Elegoo wiki')
  assert.equal(await rotation.inputValue(), 'lowers')
  assert.equal(await screw('FL').getByText(/clockwise|turns/).count(), 0, 'the reference screw is not turned')
  assert.equal(await card.getByText('The turning direction is not shown').count(), 0)
  await page.screenshot({ path: path.join(shots, 'mesh-align-turns-1440.png'), fullPage: true })

  // The settings survive a reload; the limits can be changed, and the verdict follows them.
  await page.reload()
  await card.getByRole('heading', { name: 'Mesh-guided screw alignment', exact: true }).waitFor()
  await screw('RR').getByText(`${lower} (${turns}, clockwise)`, { exact: true }).waitFor()
  await settings.getByText('Alignment settings', { exact: true }).click()
  await field('Target tilt (mm, estimate)').fill('1')
  await field('Target tilt (mm, estimate)').blur()
  await part('verdict').getByText('Alignment done, the rest is plate shape').waitFor()
  await card.getByText('Target: both tilts below 1.000 mm.').waitFor()
  await field('Shape warning above (mm, estimate)').fill('0.05')
  await field('Shape warning above (mm, estimate)').blur()
  await part('shape-warning').getByText('more than the warning limit of 0.05 mm').waitFor()
  await field('Tilt span (mm)').fill('120')
  await field('Tilt span (mm)').blur()
  await part('tilt-y').getByText(`${signed('en', tilted.tilt_y / 2)} mm`).waitFor()
  await part('tilt-y').getByText('over 120 mm').waitFor()
  await screw('RR').getByText(`${lower} (${turns}, clockwise)`, { exact: true }).waitFor()
  await settings.getByRole('button', { name: 'Reset to the CC2 defaults', exact: true }).click()
  await part('verdict').getByText('Both must be below 0.050 mm.').waitFor()
  await screw('RR').getByText(lower, { exact: true }).waitFor()
  await part('tilt-y').getByText('over 240 mm').waitFor()
  assert.equal(await part('shape-warning').count(), 0)
  assert.equal(await field('Thread pitch (mm per turn)').inputValue(), '')

  // A saved measurement of the plate library can be analysed as well.
  const measured = await source.locator('optgroup option').first().getAttribute('value')
  assert.match(measured, /^measure:/)
  await source.selectOption(measured)
  const library = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const owner = library.plates.find(p => p.measures.some(m => `measure:${m.id}` === measured))
  const saved = analyseMesh(owner.measures.find(m => `measure:${m.id}` === measured).mesh)
  await part('basis').getByText(owner.name).waitFor()
  await part('tilt-x').getByText(`${signed('en', saved.tilt_x)} mm`).waitFor()
  await part('shape')
    .getByText(`Range ${number('en', saved.residual_range, 3)} mm · RMS ${number('en', saved.residual_rms, 3)} mm`)
    .waitFor()

  // Phone width: nothing overflows.
  await page.setViewportSize({ width: 390, height: 844 })
  await card.scrollIntoViewIfNeeded()
  assert.ok(
    await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1),
    'mobile must not overflow'
  )
  await page.screenshot({ path: path.join(shots, 'mesh-align-390.png'), fullPage: true })
  await page.setViewportSize({ width: 1440, height: 1000 })

  // German: the instruction reads as specified, with a decimal comma.
  await source.selectOption('default')
  assert.deepEqual(writes, [], 'the analysis sends nothing to the printer')
  await page.evaluate(() => {
    location.hash = '#settings/appearance'
  })
  await page.locator('main select:has(option[value="de"])').selectOption('de')
  await page.waitForFunction(() => document.documentElement.lang === 'de')
  assert.deepEqual(writes, ['PUT /api/preferences'], 'only the language choice was saved')
  await page.evaluate(() => {
    location.hash = '#bed/align'
  })
  await card.getByRole('heading', { name: german['bed.align_title'], exact: true }).waitFor()
  const absenken = german['bed.align_lower'].replace('{amount}', number('de', rear, 2))
  assert.match(absenken, /^Bett an dieser Ecke um \d,\d\d mm absenken$/)
  await screw('RR').getByText(absenken, { exact: true }).waitFor()
  await screw('FL').getByText(german['bed.align_reference'], { exact: true }).waitFor()
  await part('tilt-y').getByText(`${signed('de', tilted.tilt_y)} mm`).waitFor()
  await part('verdict').getByText('Noch nicht ausgerichtet').waitFor()
  await page.screenshot({ path: path.join(shots, 'mesh-align-de-1440.png'), fullPage: true })

  assert.deepEqual(writes, ['PUT /api/preferences'], 'the German page sends nothing to the printer either')
  assert.deepEqual(errors, [])
  console.log('PASS mesh-guided screw alignment display: tilt and screws apart, heat map, verdict, shape warning, gated turns and direction, settings, German text')
} finally {
  await browser?.close()
  await server.close()
}
