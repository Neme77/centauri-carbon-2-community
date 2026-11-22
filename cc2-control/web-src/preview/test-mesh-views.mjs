// Bed alignment, step 2 with the tick "With mesh" and a saved mesh: the top view has a button that cycles four views
// (screws, 3D, 2D, plate map). 3D and 2D are the views of the Mesh tab with the screws marked; the map shows
// mesh minus plane in µm per cell, tap shows position, deviation from the plane and from the reference. Also the
// field without a mesh in step 1 and the tilt tiles. All data is synthetic; nothing is sent to the printer.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS } from '../src/lib/mesh-align.ts'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
let browser
const errors = []
const writes = []
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() !== 'GET' && !r.url().includes('/__preview/')) writes.push(`${r.method()} ${new URL(r.url()).pathname}`)
  })
  const now = Math.floor(Date.now() / 1000)
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const lib = await (await route.fetch()).json()
    for (const p of lib.plates) for (const m of p.measures) m.measured = now - 3600
    return route.fulfill({ json: lib })
  })
  // Four probes of the screws, as a finished four-corner measurement in the console.
  const out = `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${ALIGN_DEFAULTS.screws
    .map((sc, i) => `${`// probe at ${Number(sc.x).toFixed(3)},${Number(sc.y).toFixed(3)} is z=${(-0.7 + i * 0.02).toFixed(6)}\n`.repeat(3)}// Result is z=${(-0.7 + i * 0.02).toFixed(6)}\n`)
    .join('')}`
  await page.route(/\/api\/console(\?.*)?$/, route =>
    route.request().method() === 'GET'
      ? route.fulfill({ json: { busy: false, completed: true, success: true, generation: 80, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: out } })
      : route.fallback()
  )
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const library = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const mounted = library.plates.find(p => p.id === library.current)
  const reference = mounted.measures.filter(m => m.temp === 60)[0]
  const card = page.locator('#screwFocus')
  // The tick "With mesh" beside the two ways of measuring (kept in this browser).
  const tick = async on => {
    const box = page.locator('[data-level-mesh-toggle] input')
    if ((await box.isChecked()) !== on) await box.click()
  }
  const open = async choice => {
    await page.evaluate(([c, t]) => localStorage.setItem('cc2-level-base', JSON.stringify({ choice: c, temp: t })), [choice, '60'])
    await page.goto(`${origin}/#bed/align`)
    await page.reload()
    await card.waitFor()
    const b = page.locator('[data-level-step="2"] > button')
    if ((await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    await page.locator('[data-screw-measured="FL"]').waitFor({ timeout: 10000 })
    await tick(true)
  }

  // With the tick but no mesh chosen: no view button; "Measure a new mesh" takes the place of the start button and a note
  // with an info icon stands to its right, not drawn like a button.
  await open('material:PLA')
  assert.equal(await page.locator('[data-level-view-toggle]').count(), 0, 'no view button without a mesh')
  const guided = await page.locator('[data-level-guided-new]').boundingBox()
  const none = await page.locator('[data-level-saved-mesh-off]').boundingBox()
  // The button is as large as the start button without the tick (R22).
  await tick(false)
  await page.locator('[data-level-start-mode-button="session"]').click()
  const sessionStart = await page.locator('[data-screw-entries] button').first().boundingBox()
  await page.locator('[data-level-start-mode-button="quick"]').click()
  await tick(true)
  assert.ok(Math.abs(guided.width - sessionStart.width) < 2 && Math.abs(guided.height - sessionStart.height) < 2 && Math.abs(guided.x - sessionStart.x) < 2,
    `the guided button has the size and place of the session start (${guided.width}x${guided.height} / ${sessionStart.width}x${sessionStart.height})`)
  assert.ok(none.x > guided.x, 'the start button keeps its place; the note stands to its right')
  assert.doesNotMatch(await page.locator('[data-level-saved-mesh-off]').getAttribute('class'), /border/, 'the note is not drawn like a button')
  assert.equal(await page.locator('[data-level-saved-mesh-off] svg').count(), 1, 'the note carries the info icon')
  await page.locator('[data-level-saved-mesh-off]').getByText('No mesh chosen: choose one above or measure a new mesh.', { exact: true }).waitFor()
  assert.equal(await page.locator('[data-level-start-button]').count(), 0, 'without a mesh "Measure a new mesh" is the only start')

  // With a saved mesh: four views in turn.
  await open(`measure:${reference.id}`)
  const toggle = page.locator('[data-level-view-toggle]')
  await toggle.waitFor()
  assert.ok(await page.locator('[data-screw-quick-corner], [aria-label^="Single measurement"]').count() > 0, 'first the screws view')
  const frame = await page.locator('[data-level-view-toggle]').evaluate(e => e.parentElement.getBoundingClientRect().height)
  const view = async name => page.locator(`[data-level-mesh-${name}]`)
  // 1 → 3D
  await toggle.click()
  await (await view('3d')).waitFor()
  const canvas3 = page.locator('[data-level-mesh-3d] canvas')
  await canvas3.waitFor()
  const range = page.locator('[data-level-mesh-3d] input[type=range]')
  assert.equal(await range.getAttribute('min'), '.3'.replace('.', '.') === '.3' ? '.3' : '0.3')
  assert.equal(await range.getAttribute('max'), '4')
  assert.equal(await range.inputValue(), '2', 'the Z scale starts at 2×')
  await range.fill('3.5')
  await page.locator('[data-level-mesh-3d]').getByText('3.5×').waitFor()
  await (await view('3d')).getByText('drag to rotate', { exact: false }).waitFor()
  // 2D: zoom, drag moves only within the limit, double click resets.
  await toggle.click()
  const cv = page.locator('[data-level-mesh-3d] canvas')
  await (await view('3d')).getByText('scroll to zoom · drag to move', { exact: false }).waitFor()
  const b = await cv.boundingBox()
  const pixels = async () => cv.evaluate(c => c.toDataURL().length)
  const flat = await pixels()
  await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2)
  for (let i = 0; i < 8; i++) await page.mouse.wheel(0, -120)
  await page.waitForTimeout(100)
  const zoomed = await pixels()
  assert.notEqual(zoomed, flat, 'zooming changes the map')
  // Dragging far to the right: the map moves only as far as it is larger than the view.
  await page.mouse.move(b.x + 50, b.y + b.height / 2)
  await page.mouse.down()
  await page.mouse.move(b.x + b.width + 600, b.y + b.height / 2, { steps: 6 })
  await page.mouse.up()
  const pan = await cv.evaluate(c => c.__panProbe ?? null)
  const dragged = await pixels()
  assert.notEqual(dragged, zoomed, 'dragging moves the zoomed map')
  await page.mouse.move(b.x + b.width - 50, b.y + b.height / 2)
  await page.mouse.down()
  await page.mouse.move(b.x - 1500, b.y + b.height / 2, { steps: 6 })
  await page.mouse.up()
  const other = await pixels()
  await page.mouse.move(b.x + 50, b.y + b.height / 2)
  await page.mouse.down()
  await page.mouse.move(b.x + b.width + 4000, b.y + b.height / 2, { steps: 6 })
  await page.mouse.up()
  const again = await pixels()
  assert.equal(again, dragged, 'a second drag to the same far right ends at the same limit: the map cannot be lost')
  assert.notEqual(other, dragged)
  await cv.dblclick()
  await page.waitForTimeout(100)
  assert.notEqual(await pixels(), dragged, 'double click brings it back')
  void pan
  // 3 → plate map
  await toggle.click()
  const map = await view('map')
  await map.waitFor()
  const cells = map.locator('[data-align="heatmap"] g[role="button"]')
  assert.ok((await cells.count()) >= 9, 'a cell per mesh point')
  assert.ok((await map.locator('text').count()) >= 9, 'a µm number in every cell')
  await map.getByText('Numbers in the cells', { exact: false }).waitFor()
  await cells.nth(4).click()
  const picked = map.locator('[data-align="picked"]')
  await picked.getByText('Position', { exact: true }).waitFor()
  await picked.getByText('Deviation from the plane', { exact: true }).waitFor()
  await picked.getByText('Deviation from the reference', { exact: true }).waitFor()
  await map.getByText('Mesh minus plane', { exact: false }).waitFor()
  await map.getByText(/Range [\d.]+ mm · RMS [\d.]+ mm/).waitFor()
  assert.equal(await map.locator('[data-align="heatmap"]').evaluate(e => e.getBoundingClientRect().height > 100), true)
  // 4 → back to the screws
  await toggle.click()
  assert.equal(await page.locator('[data-level-mesh-map], [data-level-mesh-3d]').count(), 0)
  await page.locator('[aria-label^="Single measurement"]').first().waitFor()
  // The frame of the top view keeps no wild size between the views.
  assert.ok(Math.abs((await toggle.evaluate(e => e.parentElement.getBoundingClientRect().height)) - frame) < 80, 'the frame of the view stays about the same')
  // Tilt tiles instead of the line.
  await page.locator('[data-align="tilt-x"]').waitFor()
  await page.locator('[data-align="tilt-y"]').waitFor()
  // The same screw symbol in every view: 26 px round badge (the map and canvases draw r=11/13; the top view has a size-6.5 badge).
  // 150 % text at 390 px: no sideways scrolling in any view.
  await page.setViewportSize({ width: 390, height: 844 })
  await page.reload()
  await card.waitFor()
  await page.evaluate(() => (document.documentElement.style.fontSize = '150%'))
  await tick(true)
  for (let i = 0; i < 4; i++) {
    await page.waitForTimeout(150)
    const over = await page.evaluate(() => (document.documentElement.scrollWidth <= innerWidth + 1 ? [] : [...document.querySelectorAll('#screwFocus *')].filter(e => e.getBoundingClientRect().right > innerWidth + 1).slice(0, 5).map(e => `${e.tagName}.${String(e.className).slice(0, 50)} ${Math.round(e.getBoundingClientRect().right)}`)))
    assert.deepEqual(over, [], `no sideways scrolling in view ${i + 1} at 150 %`)
    if (await toggle.count()) await toggle.click()
  }
  assert.deepEqual(writes, [], 'nothing is sent to the printer')
  assert.deepEqual(errors, [])
  console.log('PASS mesh views: screws, 3D, 2D (zoom, limited moving, double click), plate map with tap, empty field without a mesh, tilt tiles, 150 % at 390 px')
} finally {
  await browser?.close()
  await server.close()
}
