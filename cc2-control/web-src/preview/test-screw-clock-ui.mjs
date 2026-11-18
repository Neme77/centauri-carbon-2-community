// Turns of the CC2's own fixed screw (M4, 0.7 mm, clockwise from below raises) in step 3 of the Bed alignment tab:
// the exact angle next to the arrow (plain plan only), the legend in the help, old stored thread values ignored,
// German text, phone width, accessibility. Probe values are synthetic; the expected numbers are worked out by hand.
// The settings block without thread controls is checked in test-mesh-align.mjs.
import './test-screw-clock.mjs'
import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
const german = JSON.parse(await readFile(path.join(root, 'public/locales/de.json'), 'utf8'))
// axe-core is no dependency of the project: the check runs when CC2_AXE_PATH names it, as in the other preview tests.
const axePath = process.env.CC2_AXE_PATH
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
const shots = process.env.CC2_SCREENSHOTS
const shoot = async (page, name) => {
  if (!shots) return
  await mkdir(shots, { recursive: true })
  await page.screenshot({ path: path.join(shots, name), fullPage: true })
}
const errors = []
const writes = []
let browser
try {
  await server.listen()
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
  await page.route('**/api/experiments', route =>
    route.fulfill({
      json: { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, mesh_align_settings: true },
    })
  )
  // No mesh basis: the plain four-point plan (the plate shape would change the turns).
  await page.addInitScript(() => { try { localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' })); localStorage.setItem('cc2-screw-turn-mode', 'exact') } catch {} })
  await page.route('**/i18n/de.json', route => route.fulfill({ json: german }))
  // The four-point tab reads the console output of the last one-shot measurement.
  let probeZ = null
  const probeText = z =>
    [[27.9, 25.95], [227.15, 25.95], [227.15, 225.5], [27.9, 225.5]]
      .flatMap(([x, y], i) => [1, 2, 3].map(() => `probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${z[i].toFixed(3)}`))
      .join('\n')
  await page.route('**/api/console', route => {
    if (!probeZ || route.request().method() !== 'GET') return route.fallback()
    return route.fulfill({
      json: {
        busy: false,
        completed: true,
        success: true,
        generation: 1,
        command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE',
        output: probeText(probeZ),
      },
    })
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Bed alignment', exact: true }).click()
  const four = page.locator('#screwFocus')
  await four.waitFor()
  const unfold = async () => {
    for (const n of [2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
  }
  await unfold()
  assert.equal(await four.locator('[data-screw-exact]').count(), 0, 'no measurement yet: no turns')
  probeZ = [1.8, 1.85, 1.77, 1.81]
  // The table shows every height as measured (Z height); the turn column holds the difference: FR is 50 µm high.
  await four.locator('[data-screw-zheight="FR"]').getByText('Z height: 1850 µm').waitFor({ timeout: 8000 })
  // FR is 50 µm high: lower it, 0.05 mm / 0.7 mm per turn = 25.7 degrees (0.1 degree is about 0.2 µm).
  await four.locator('[data-screw-exact="FR"]').getByText('25.7°', { exact: true }).waitFor()
  // RR is 30 µm low: raise it, 15.4 degrees.
  await four.locator('[data-screw-exact="RR"]').getByText('15.4°', { exact: true }).waitFor()
  assert.equal(await four.locator('[data-screw-exact="FL"]').count(), 0, 'the reference corner is not turned')
  assert.equal(await four.locator('[data-screw-exact="RL"]').count(), 0, 'RL is within the 20 µm tolerance')
  await four.locator('[data-screw-possible="RL"]').getByText('Still possible:', { exact: false }).waitFor()
  await four.locator('details[data-screw-help] summary').click() // the legend sits in the folded help
  await four.locator('[data-screw-turn-legend]').getByText('One click on the screw tool = 5°, 72 clicks make one turn.').waitFor()
  for (const mode of ['auto', 'exact']) await four.locator(`[data-screw-help-mode="${mode}"]`).waitFor()
  // The mode names are set in bold, their description names no clicks: it holds for every turn unit.
  assert.equal(await four.locator('[data-screw-help-mode="auto"] strong').innerText(), 'Auto')
  assert.equal(await four.locator('[data-screw-help-mode="exact"] strong').innerText(), 'Exact')
  await four.locator('[data-screw-help-mode="auto"]').getByText('for example 1/12 of a turn', { exact: false }).waitFor()
  assert.doesNotMatch(await four.locator('[data-screw-help-modes]').innerText(), /click/i)
  await four.locator('[data-screw-help-sleeve]').getByText('sleeve', { exact: false }).waitFor()
  {
    // The help: Auto and Exact first and side by side, then the click legend, the three warnings last in an orange frame, bright text.
    const at = async sel => four.locator(sel).boundingBox()
    const [auto, exact, warn, legend] = [await at('[data-screw-help-mode="auto"]'), await at('[data-screw-help-mode="exact"]'), await at('[data-screw-help-warn]'), await at('[data-screw-turn-legend]')]
    assert.ok(Math.abs(auto.y - exact.y) < 2 && exact.x > auto.x + auto.width, 'Auto and Exact share one row')
    assert.ok(auto.y < legend.y && legend.y < warn.y, 'modes first, click legend, the warnings last')
    assert.equal(await four.locator('[data-screw-help-warn] [data-screw-help-sleeve], [data-screw-help-warn] [data-screw-help-seated], [data-screw-help-warn] [data-screw-help-rounds]').count(), 3)
    // One frame: the title box is open at its bottom and the text box starts right under it, without a gap.
    const title = await at('details[data-screw-help]'), body = await at('[data-screw-help-body]')
    assert.ok(Math.abs(title.y + title.height - body.y) < 1.5, 'title and text touch')
    assert.equal(await four.locator('details[data-screw-help]').evaluate(el => getComputedStyle(el).borderBottomWidth), '0px', 'no line between title and text')
    const colours = await four.locator('[data-screw-help-warn]').evaluate(el => {
      const frame = getComputedStyle(el), text = getComputedStyle(el.parentElement), muted = getComputedStyle(document.querySelector('[data-screw-from-below]'))
      return { border: frame.borderTopColor, text: text.color, muted: muted.color }
    })
    assert.notEqual(colours.text, colours.muted, 'the help text is brighter than the muted notes')
    assert.equal(await four.getByText('3 probes per measurement', { exact: false }).count(), 0, 'the probe sentence is gone from the help')
    await four.locator('[data-screw-help-tolerance]').getByText('Within ±20 µm a corner counts as within tolerance', { exact: false }).waitFor()
  }
  assert.equal(await four.locator('[data-screw-combined-note]').count(), 0)
  await shoot(page, 'screw-clock-four-1440.png')
  // The combined plan (front left moves too) shows its turn amounts as well, the front-left screw included, and its note
  // stands above the table.
  probeZ = [1.8, 1.74, 1.72, 1.7]
  await four.locator('[data-screw-combined-note]').waitFor({ timeout: 8000 })
  await four.locator('[data-screw-exact="FL"]').waitFor()
  {
    const note = await four.locator('[data-screw-combined-note]').boundingBox()
    const table = await four.locator('table').first().boundingBox()
    assert.ok(note.y + note.height <= table.y + 1, 'the note of the combined plan stands above the table')
  }
  // Old stored thread settings (other pitch, direction, cleared values) do not change the CC2 screw.
  for (const stale of [
    { thread_pitch_mm: 0.5, thread_pitch_source: 'old', clockwise_raises_bed: false, rotation_source: 'Elegoo wiki', thread_preset: 'CCW-M3' },
    { thread_pitch_mm: null, thread_pitch_source: '', clockwise_raises_bed: null, rotation_source: '', thread_preset: null },
  ]) {
    probeZ = [1.8, 1.85, 1.77, 1.81]
    await page.evaluate(v => localStorage.setItem('cc2-mesh-align', JSON.stringify(v)), stale)
    await page.reload()
    await four.waitFor()
    await unfold()
    await four.locator('[data-screw-zheight="FR"]').getByText('Z height: 1850 µm').waitFor({ timeout: 8000 })
    await four.locator('[data-screw-exact="FR"]').getByText('25.7°', { exact: true }).waitFor()
    assert.equal(await four.locator('[data-screw-combined-note]').count(), 0)
  }

  // Phone width and accessibility, light and dark.
  for (const [width, theme] of [[1440, 'light'], [390, 'light'], [390, 'dark']]) {
    await page.setViewportSize({ width, height: 900 })
    await page.evaluate(v => {
      document.documentElement.dataset.theme = v
    }, theme)
    await four.scrollIntoViewIfNeeded()
    await page.waitForTimeout(600) // the colours fade between the themes
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `no overflow at ${width}`)
    if (axePath) {
      await page.addScriptTag({ path: axePath })
      assert.deepEqual(
        await page.evaluate(async () => (await axe.run(document.querySelector('#screwFocus'))).violations.map(v => ({ id: v.id, nodes: v.nodes.map(n => n.target) }))),
        [],
        `accessibility of the tab at ${width}/${theme}`
      )
    }
  }
  await shoot(page, 'screw-clock-four-390.png')
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.evaluate(() => {
    document.documentElement.dataset.theme = 'light'
  })

  // German: the same numbers, German words around them.
  assert.deepEqual(writes, [], 'nothing was sent to the printer')
  await page.evaluate(() => {
    location.hash = '#settings/appearance'
  })
  await page.locator('main select:has(option[value="de"])').selectOption('de')
  await page.waitForFunction(() => document.documentElement.lang === 'de')
  await page.evaluate(() => {
    location.hash = '#bed/align'
  })
  await four.getByRole('heading', { name: german['bed.level_title'], exact: true }).waitFor()
  await unfold()
  await four.locator('[data-screw-exact="FR"]').getByText('25,7°', { exact: true }).waitFor()
  assert.match(german['bed.turn_legend'], /Klick/)
  assert.deepEqual(writes, ['PUT /api/preferences'], 'only the language choice was saved')
  assert.deepEqual(errors, [])
  console.log('PASS screw clock display: fixed CC2 screw, turns in step 3, legend, old stored values ignored, phone width, axe, German')
} finally {
  await browser?.close()
  await server.close()
}
