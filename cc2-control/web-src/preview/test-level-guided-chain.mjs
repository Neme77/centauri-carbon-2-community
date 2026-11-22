// Bed alignment with the tick "With mesh" (R15): "Measure a new mesh" measures ONE mesh and then the four screw points
// by itself (here the way "Quick"; the session start after the mesh is covered by test-bed-mesh-tick); after that only
// the screws are measured again. That first mesh is only the plate's working mesh: the library
// stays as it was, the evaluation uses the working mesh, and only the final mesh (asked for once every corner is within
// tolerance) replaces the stored one. Until then the page says that the working mesh is temporary, and once the screws
// were turned (screw measurement against screw measurement) it warns that the stored mesh no longer fits the bed.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import fs from 'node:fs'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS, fitPlane, gridPoints, meshAt, planeAt } from '../src/lib/mesh-align.ts'

const lines = heights =>
  Object.entries(heights)
    .map(([key, z]) => {
      const [x, y] = key.split(',')
      const h = Number(z).toFixed(6)
      return `${`// probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${h}\n`.repeat(3)}// Result is z=${h}\n`
    })
    .join('')
const job = (generation, heights) => ({
  busy: false,
  completed: true,
  success: true,
  generation,
  command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n',
  output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${lines(heights)}`,
})

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
const errors = []
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
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  // Every load starts from a typed 60 °C, unless a case keeps the basis it chose (cc2-test-keep-base).
  await page.addInitScript(() => {
    try {
      if (!localStorage.getItem('cc2-test-keep-base'))
        localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' }))
    } catch {}
  })
  const keepBase = on =>
    page.evaluate(v => {
      try {
        if (v) localStorage.setItem('cc2-test-keep-base', '1')
        else localStorage.removeItem('cc2-test-keep-base')
      } catch {}
    }, on)
  page.on('pageerror', e => errors.push(e.message))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  let consoleJob = { busy: false, completed: false, success: false, generation: 60, command: '', output: '' }
  // The console as the printer reports it: busy with the steps of a running mesh calibration (the preview's own answer),
  // otherwise the screw measurement of this test.
  await page.route(/\/api\/console(\?.*)?$/, async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const live = await (await route.fetch()).json()
    if (live.busy && !consoleJob.busy && !/CC2_SCREW/.test(live.command))
      return route.fulfill({ json: { ...consoleJob, busy: true, completed: false, command: live.command } })
    return route.fulfill({ json: consoleJob })
  })
  const actions = []
  const actionTimes = []
  let refuse = 0
  // A refusal that lasts until a point in time, with its own status and reason (the chain cases below).
  let refuseUntil = 0
  let refuseWith = null
  // A quick screw measurement, also with a hold time in front (the starts the user makes take the one of step 1).
  const isMeasure = body => /^(screws:soak:\d+:)?screws:measure/.test(body)
  await page.route('**/api/control', route => {
    const body = route.request().postData() || ''
    if (route.request().method() !== 'POST' || !isMeasure(body)) return route.fallback()
    actions.push(body)
    actionTimes.push(Date.now())
    if (refuseWith && Date.now() < refuseUntil)
      return route.fulfill({ status: refuseWith.status, json: { accepted: false, error: refuseWith.error } })
    if (refuse) {
      refuse--
      return route.fulfill({ status: 409, json: { accepted: false, error: 'Screw measurement requires the printer to be idle' } })
    }
    return route.fulfill({ status: 202, json: { accepted: true } })
  })
  // The screw history the page reads (null: the preview's own).
  let history = null
  await page.route('**/api/screws/history', route =>
    history && route.request().method() === 'GET' ? route.fulfill({ json: { sessions: history } }) : route.fallback()
  )
  const quick = (time, z) => ({
    kind: 'quick',
    started: time,
    ended: time,
    points: 'screws',
    measurements: [{ time, kind: 'quick', corner: null, twice: false, z }],
  })
  // CC2 Control restarted: it reports no working mesh any more.
  let forgetWork = false
  await page.route('**/api/plates', async route => {
    if (!forgetWork || route.request().method() !== 'GET') return route.fallback()
    const response = await route.fetch()
    const json = await response.json()
    json.calibration.work_mesh = null
    return route.fulfill({ response, json })
  })
  const calibrations = []
  page.on('request', r => {
    if (r.method() === 'POST' && new URL(r.url()).pathname === '/api/plates/calibrate') calibrations.push(r.postData() || '')
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.goto(`${origin}/#bed/align`)
  const card = page.locator('#screwFocus')
  await card.waitFor()
  for (const n of [1, 2]) {
    const b = page.locator(`[data-level-step="${n}"] > button`)
    if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
  }
  // The tick "With mesh" beside the two ways of measuring (kept in this browser).
  const meshTick = page.locator('[data-level-mesh-toggle] input')
  const tick = async on => {
    if ((await meshTick.isChecked()) !== on) await meshTick.click()
    assert.equal(await meshTick.isChecked(), on)
  }
  await page.locator('[data-level-start-mode-button="quick"]').click()
  await tick(true)
  const dialog = page.getByRole('dialog')
  const plates = () => page.evaluate(async () => (await fetch('/api/plates')).json())
  const step = () => page.evaluate(() => fetch('/__preview/calibration-step', { method: 'POST', headers: { 'X-CC2-Request': '1' } }))

  // Step 1 holds the hold time beside the temperature: the same list and the same setting as the Mesh tab, 0 to the
  // limit of the printer service (PLATE_SOAK_MAX), so nothing chosen is refused later.
  const soakMax = Number(fs.readFileSync(path.join(root, '../src/plates.h'), 'utf8').match(/#define PLATE_SOAK_MAX (\d+)/)[1])
  const soakMaxWeb = Number(fs.readFileSync(path.join(root, 'src/components/calibration-progress.tsx'), 'utf8').match(/SOAK_MAX_MIN = (\d+)/)[1])
  assert.equal(soakMaxWeb, soakMax, 'the limit of the page is the one of the printer service')
  const soak = page.locator('[data-level-step="1"] label', { hasText: 'Soak, min' }).locator('select')
  await soak.waitFor()
  assert.deepEqual(await soak.locator('option').allInnerTexts(), Array.from({ length: soakMax + 1 }, (_, i) => String(i)), 'the list of the Mesh tab: 0 to the limit')
  await soak.selectOption('10')
  // The Mesh tab shows the same setting.
  await page.evaluate(() => (location.hash = '#bed/mesh'))
  const meshSoak = page.locator('label', { hasText: 'Soak, min' }).locator('select')
  await meshSoak.waitFor()
  assert.equal(await meshSoak.inputValue(), '10', 'the Mesh tab shows the hold time of step 1')
  await page.evaluate(() => (location.hash = '#bed/align'))
  await soak.waitFor()
  assert.equal(await soak.inputValue(), '10')

  // The start says what it does; declined, nothing is sent.
  const guided = page.locator('[data-level-guided-new]')
  await guided.getByText('Temporary mesh, then the screw measurement, heaters off afterwards', { exact: true }).waitFor()
  await guided.click()
  // The plate's name is a choice in the question: the saved plates, the plate of step 1 preselected.
  const question = dialog.locator('[data-confirm-text]')
  await question.getByText(/Measure a temporary mesh at 60 °C for/).waitFor()
  await question.getByText(/and the four screw points right after it\? The temporary mesh only serves the evaluation; the saved mesh of the plate is replaced only by the final mesh\. After turning, only the screws are measured again\./).waitFor()
  await question.getByText(/Temporary mesh – it is lost when CC2 Control restarts or a new mesh is measured\./).waitFor()
  const plateChoice = dialog.locator('[data-level-confirm-plate]')
  const platesNow = (await page.evaluate(async () => (await fetch('/api/plates')).json())).plates
  assert.deepEqual(await plateChoice.locator('option').allInnerTexts(), platesNow.map(p => p.name), 'every saved plate can be chosen')
  const preselected = await plateChoice.inputValue()
  assert.ok(platesNow.some(p => p.id === preselected), 'a saved plate is preselected')
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  assert.equal(calibrations.length, 0, 'a declined start sends nothing')
  assert.equal(await card.locator('[data-level-chain]').count(), 0)

  // Confirmed: one calibration is started, the page says that the screw points follow, the start buttons are locked.
  // What the library file holds; where the printer side mesh and the printer profiles stand is the printer's (the slot
  // measurement keeps its profile before every calibration).
  const strip = lib => ({
    current: lib.current,
    plates: lib.plates.map(({ in_printer, measures, ...p }) => ({
      ...p,
      measures: measures.map(({ slot, profile, ...m }) => m),
    })),
  })
  const libraryBefore = strip(await plates())
  await guided.click()
  assert.equal(await plateChoice.inputValue(), preselected, 'the choice starts from the same plate again')
  // The choice survives the live refresh of the question (twice a second).
  await page.waitForTimeout(1200)
  assert.equal(await plateChoice.inputValue(), preselected)
  await question.getByText('The bed heats to 60 °C and is held there for 10 min before probing.').waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.locator('[data-level-chain-bar="mesh"]').getByText('The new mesh is being measured. The four screws are measured after it.').waitFor({ timeout: 10000 })
  assert.equal(await page.locator('[data-level-chain-bar="mesh"] [data-level-chain-cancel]').count(), 0, 'no cancel button in the bar while the mesh runs: the stop is the one of the calibration bar')
  assert.equal(calibrations.length, 1, 'one calibration')
  assert.match(calibrations[0], /^[AB]\n60\n10\n[0-9a-f]*\n[0-9a-f]+\nwork$/, 'side, temperature, the hold time of the question (10), nozzle, the plate and "only a working mesh"')
  assert.equal(await guided.isDisabled(), true, 'no second start while the chain runs')
  // The bar of the chain is the only display of the chain: the tab has no line of its own.
  assert.equal(await card.locator('[data-level-chain]').count(), 0, 'no chain line in the tab while the chain runs')
  assert.equal(await page.locator('[data-level-chain-bar]').count(), 1, 'one bar of the chain')
  // The bar of the mesh calibration stands above the tabs, and this mesh can be stopped from there like any other.
  await page.locator('[data-calibration-bar]').waitFor({ timeout: 10000 })
  await page.locator('[data-calibration-stop]').waitFor({ timeout: 10000 })
  // ONE field: the chain's line on top, under it the calibration, smaller, with Stop; no second calibration line.
  const chainField = page.locator('[data-level-chain-bar="mesh"]')
  assert.equal(await chainField.locator('[data-calibration-bar][data-calibration-compact] [data-calibration-stop]').count(), 1, 'the calibration with Stop stands in the field of the chain')
  assert.equal(await page.locator('[data-calibration-bar]').count(), 1, 'one calibration line')
  assert.equal(await page.locator('[data-mesh-status]').count(), 1, 'one field')
  assert.equal(await chainField.getAttribute('role'), 'status')
  // "Bed alignment" leads to the tab, and is not shown there.
  assert.equal(await chainField.locator('[data-mesh-status-open="align"]').count(), 0, 'no "Bed alignment" in the Bed alignment tab')
  await page.evaluate(() => {
    location.hash = '#dashboard'
  })
  await chainField.locator('[data-calibration-stop]').waitFor({ timeout: 10000 })
  // 390 px: the field with both lines wraps without sideways scrolling.
  await page.setViewportSize({ width: 390, height: 844 })
  await page.waitForTimeout(300)
  const tooWide = await page.evaluate(() => [...document.querySelectorAll('[data-mesh-status], [data-mesh-status] *')].filter(e => e.getBoundingClientRect().right > innerWidth + 1).length + (document.documentElement.scrollWidth > innerWidth + 1 ? 1 : 0))
  assert.equal(tooWide, 0, 'no sideways scrolling with the field of the chain at 390 px')
  await page.setViewportSize({ width: 1440, height: 1100 })
  const toAlign = chainField.getByRole('button', { name: 'Bed alignment', exact: true })
  await toAlign.click()
  await page.waitForFunction(() => location.hash === '#bed/align')
  await chainField.locator('[data-mesh-status-open="align"]').waitFor({ state: 'detached' })
  await card.waitFor()
  assert.equal(actions.length, 0, 'no screw measurement while the mesh is measured')
  // The console is busy with the steps of the mesh, but no screw measurement runs: the tab does not say so (the bar of
  // the calibration does).
  await page.waitForTimeout(3000)
  assert.equal(await card.locator('[data-screw-status]').count(), 0, 'no "measurement running" line during the mesh')
  assert.equal(await card.getByText('Probing…', { exact: true }).count(), 0, 'no "Probing…" in the card head during the mesh')

  // The calibration runs to its end with the working mesh; the first start of the screw measurement is refused (printer
  // still busy), the page tries again by itself and says nothing about the refusal.
  refuse = 1
  for (let i = 0; i < 10 && (await plates()).calibration.stage !== 'off'; i++) {
    await step()
    await page.waitForTimeout(300)
  }
  const library = await plates()
  assert.equal(library.calibration.result, 'work', 'the first mesh ends as a working mesh')
  const work = library.calibration.work_mesh
  assert.ok(work && work.plate === preselected && work.temp === 60, 'CC2 Control reports the working mesh of the plate')
  assert.deepEqual(strip(library), libraryBefore, 'the library, the mounted plate and every stored measurement stay as they were')
  await page.waitForFunction(() => true)
  for (let i = 0; i < 60 && actions.length < 2; i++) await page.waitForTimeout(500)
  assert.equal(actions.length, 2, 'refused once, then accepted')
  assert.equal(actions[0], 'screws:measure:60:2')
  assert.equal(actions[1], 'screws:measure:60:2')
  assert.equal(await page.getByText('Rejected:', { exact: false }).count(), 0, 'the refused try is not shown as an error')
  await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 10000 })
  await page.waitForTimeout(6000)
  assert.equal(actions.length, 2, 'once accepted, nothing more is sent')
  assert.equal(calibrations.length, 1, 'the mesh is measured once')

  // The working mesh is the basis of step 1, with the note that it is temporary; nothing warns yet.
  assert.equal(await page.locator('[data-level-base]').inputValue(), 'work', 'the working mesh is the basis')
  await card.locator('[data-level-work-note]').getByText('Temporary mesh – it is lost when CC2 Control restarts or a new mesh is measured.').waitFor()
  assert.equal(await page.locator('[data-level-base] optgroup[label="Temporary mesh (not saved)"] option').count(), 1)
  await card.locator('[data-level-start-button="quick"]').getByText('Evaluated with the temporary mesh').waitFor()
  // The screw points come in so that every corner sits on the plate shape of the working mesh: within tolerance. The
  // page asks for the final mesh, and the 3D view shows the mesh carried along, marked as calculated.
  const measure = work
  const plane = fitPlane(gridPoints(measure.mesh, ALIGN_DEFAULTS.row0_is_min_y))
  const onShape = offset =>
    Object.fromEntries(
      ALIGN_DEFAULTS.screws.map(sc => [
        `${sc.x},${sc.y}`,
        offset + meshAt(measure.mesh, sc.x, sc.y, ALIGN_DEFAULTS.row0_is_min_y) - planeAt(plane, sc.x, sc.y),
      ])
    )
  consoleJob = job(61, onShape(-0.4))
  // "Screws turned" is judged from screw measurements alone: the first one after the working mesh is the reference.
  // Rear right lies 400 µm off front left there.
  const corners = (fr, rr, rl) => ({ FL: -0.4, FR: -0.4 + fr, RR: -0.4 + rr, RL: -0.4 + rl })
  history = [quick(work.measured + 100, corners(0.2, 0.4, 0.05))]
  await page.waitForTimeout(6000)
  assert.equal(await card.locator('[data-level-work-stale]').count(), 0, 'one screw measurement after the working mesh: no warning')
  // A second one that differs by 20 µm at most (rear right): below 25 µm, the screws were not turned.
  history = [quick(work.measured + 200, corners(0.204, 0.42, 0.052)), ...history]
  await page.waitForTimeout(6000)
  assert.equal(await card.locator('[data-level-work-stale]').count(), 0, 'measured again without turning: no warning')
  // Rear right turned by 80 µm (a fifth of its 400 µm offset): 25 µm or more counts as turned, however far off the corner
  // was, so the stored mesh no longer fits the bed.
  history = [quick(work.measured + 300, corners(0.204, 0.48, 0.052)), ...history]
  const stale = card.locator('[data-level-work-stale]')
  const staleText = 'The screws were turned. The mesh in the printer no longer matches the bed. Measure the final mesh to finish.'
  await stale.getByText(staleText, { exact: true }).waitFor({ timeout: 10000 })
  // The same warning for a plate that has no saved measurement yet (the old text spoke of a saved mesh).
  const noSaved = async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const response = await route.fetch()
    const json = await response.json()
    for (const p of json.plates) if (p.id === preselected) p.measures = []
    return route.fulfill({ response, json })
  }
  await page.route('**/api/plates', noSaved)
  await page.evaluate(() => (location.hash = '#dashboard'))
  await page.waitForTimeout(500)
  await page.evaluate(() => (location.hash = '#bed/align'))
  await page.waitForTimeout(2500)
  await stale.getByText(staleText, { exact: true }).waitFor({ timeout: 15000 })
  await page.unroute('**/api/plates', noSaved)
  await page.evaluate(() => (location.hash = '#dashboard'))
  await page.waitForTimeout(500)
  await page.evaluate(() => (location.hash = '#bed/align'))
  await stale.waitFor({ timeout: 15000 })
  // The column "Plate shape" (only with a mesh as the basis) explains itself with an (i), by keyboard as well.
  const shapeHint = card.locator('[data-screw-measured-block] [data-hint="shape"] button')
  await shapeHint.waitFor({ timeout: 10000 })
  await shapeHint.focus()
  await page.keyboard.press('Enter')
  await page.locator('[data-hint-text]').getByText('Plate shape: the warp of the plate at this corner, from the mesh. Difference: the part the screw can even out.', { exact: true }).waitFor()
  await page.mouse.move(0, 0) // no hover: only the keyboard opened it
  await page.keyboard.press('Enter')
  await page.locator('[data-hint-text]').waitFor({ state: 'detached' })
  // Without the tick there is neither the column nor its (i).
  await tick(false)
  await card.locator('[data-hint="shape"]').waitFor({ state: 'detached' })
  assert.equal(await card.locator('thead th', { hasText: 'Plate shape' }).count(), 0, 'no column without a mesh')
  await tick(true)
  await shapeHint.waitFor()

  // The marks 1–4 and the rows of the table pick one corner (display only): a cyan outline on the row, the mark
  // highlighted; a second click lifts it; at most one corner. 1 = front left, 2 = front right, 3 = rear right, 4 = rear left.
  const row = id => card.locator(`[data-screw-row="${id}"]`)
  const pickedRows = () => card.locator('[data-screw-row][data-picked]').evaluateAll(es => es.map(e => e.getAttribute('data-screw-row')))
  const mark3 = card.locator('[data-screw-pick-mark="RR"]')
  assert.equal(await mark3.innerText(), '3')
  await mark3.click()
  assert.deepEqual(await pickedRows(), ['RR'], 'mark 3 picks the row "Rear right"')
  assert.match(await row('RR').innerText(), /^3\s*Back right/)
  assert.equal(await mark3.getAttribute('aria-pressed'), 'true')
  await mark3.click()
  assert.deepEqual(await pickedRows(), [], 'a second click lifts it')
  // The other way: the row picks the mark; by keyboard as well, and only one corner at a time.
  await row('FR').locator('td').nth(1).click()
  assert.equal(await card.locator('[data-screw-pick-mark="FR"]').getAttribute('aria-pressed'), 'true', 'the row picks mark 2')
  await card.locator('[data-screw-pick-row="RL"]').focus()
  await page.keyboard.press('Enter')
  assert.deepEqual(await pickedRows(), ['RL'], 'one corner at a time')
  await page.keyboard.press(' ')
  assert.deepEqual(await pickedRows(), [])
  for (const [id, n] of [['FL', '1'], ['FR', '2'], ['RR', '3'], ['RL', '4']]) assert.equal(await card.locator(`[data-screw-pick-row="${id}"]`).innerText(), n)
  // The map view: the marks sit at the screws of lib/mesh-align (front edge at the bottom) and pick the rows too.
  const toggle = card.locator('[data-level-view-toggle]')
  for (let i = 0; i < 4 && !(await card.locator('[data-level-mesh-map]').count()); i++) await toggle.click()
  const svg = card.locator('[data-level-mesh-map] svg')
  await svg.waitFor()
  // Where a screw (mm) lands on the screen: the SVG's own transform, the top edge taken from a mesh cell (its rect stands
  // at y = top − p.y − step/2, its label names p).
  const offsets = await svg.evaluate(el => {
    const cell = el.querySelector('g[role="button"][aria-label^="X"]')
    const [, px, py] = cell.getAttribute('aria-label').match(/^X(-?[\d.]+) Y(-?[\d.]+)/).map(Number)
    const r = cell.querySelector('rect')
    const top = Number(r.getAttribute('y')) + py + (Number(r.getAttribute('height')) - 0.3) / 2
    const ctm = el.getScreenCTM()
    return [['FL', 27.9, 25.95], ['FR', 227.15, 25.95], ['RR', 227.15, 225.5], ['RL', 27.9, 225.5]].map(([id, x, y]) => {
      const pt = new DOMPoint(x, top - y).matrixTransform(ctm)
      const c = el.querySelector(`[data-screw-pick-map="${id}"] circle`).getBoundingClientRect()
      return { id, n: el.querySelector(`[data-screw-pick-map="${id}"] text`).textContent, dx: c.x + c.width / 2 - pt.x, dy: c.y + c.height / 2 - pt.y, sx: pt.x, sy: pt.y }
    })
  })
  for (const o of offsets) assert.ok(Math.abs(o.dx) <= 2 && Math.abs(o.dy) <= 2, `mark ${o.id} at its screw (${o.dx.toFixed(1)}, ${o.dy.toFixed(1)} px)`)
  assert.deepEqual(offsets.map(o => o.n), ['1', '2', '3', '4'], 'FL 1, FR 2, RR 3, RL 4')
  // The marks of the map are exact circles too (width == height, 0.5 px).
  const mapMarks = await svg.evaluate(el => [...el.querySelectorAll('[data-screw-pick-map] circle')].map(c => { const r = c.getBoundingClientRect(); return [r.width, r.height] }))
  assert.equal(mapMarks.length, 4)
  for (const [w, h] of mapMarks) assert.ok(Math.abs(w - h) <= 0.5, `map mark is round (${w.toFixed(2)} x ${h.toFixed(2)} px)`)
  const at = Object.fromEntries(offsets.map(o => [o.id, o]))
  assert.ok(at.FL.sy > at.RL.sy && at.FR.sy > at.RR.sy, 'front edge at the bottom')
  assert.ok(at.FL.sx < at.FR.sx && at.RL.sx < at.RR.sx, 'left on the left')
  console.log(`map marks against the screws: ${offsets.map(o => `${o.id} ${o.dx.toFixed(2)}/${o.dy.toFixed(2)} px`).join(', ')}`)
  await card.locator('[data-screw-pick-map="RR"]').click()
  assert.deepEqual(await pickedRows(), ['RR'], 'mark 3 of the map picks "Rear right"')
  assert.equal(await card.locator('[data-screw-pick-map="RR"]').getAttribute('aria-pressed'), 'true')
  await row('RR').locator('td').nth(1).click()
  assert.deepEqual(await pickedRows(), [], 'the row lifts it again')
  assert.equal(await card.locator('[data-screw-pick-map="RR"]').getAttribute('aria-pressed'), 'false')
  for (let i = 0; i < 4 && !(await card.locator('[data-screw-pick-mark="FL"]').count()); i++) await toggle.click()
  const ask = card.locator('[data-level-final-ask]')
  await ask.getByText('All corners are within tolerance. Create the final mesh?').waitFor({ timeout: 15000 })
  await card.locator('[data-level-view-toggle]').click()
  await card.locator('[data-level-carried]').getByText('Calculated: the chosen mesh carried along to the latest screw measurement. Not measured.').waitFor({ timeout: 8000 })

  // There is no "Not now": the question stays as long as the latest measurement is within the tolerance, also after
  // another page, a reload and the next measurement.
  assert.equal(await ask.locator('button').count(), 1, 'one answer only: no "Not now"')
  assert.equal(await ask.locator('[data-level-final-no]').count(), 0)
  await page.evaluate(() => (location.hash = '#dashboard'))
  await page.waitForTimeout(500)
  await page.evaluate(() => (location.hash = '#bed/align'))
  await ask.waitFor({ timeout: 15000 })
  await keepBase(true)
  await page.reload()
  await card.waitFor()
  for (const n of [1, 2]) {
    const b = page.locator(`[data-level-step="${n}"] > button`)
    if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
  }
  await ask.waitFor({ timeout: 15000 })
  await keepBase(false)
  consoleJob = job(62, onShape(-0.402))
  await page.waitForTimeout(3000)
  await ask.waitFor({ timeout: 15000 })

  // "Yes": the final mesh is asked for and measured; no screw measurement follows it.
  // The final mesh takes the hold time of step 1 as well (here the limit), and the question names it.
  await soak.selectOption(String(soakMax))
  await ask.locator('[data-level-final-yes]').click()
  await question.getByText(/Measure the final mesh at 60 °C for/).waitFor()
  await question.getByText(`The bed heats to 60 °C and is held there for ${soakMax} min before probing.`).waitFor()
  await question.getByText(/\? It replaces the saved mesh of this plate at this temperature and nozzle\./).waitFor()
  assert.equal(await question.getByText(/calculated mesh/).count(), 0, 'the question no longer names the calculated mesh')
  assert.equal(await question.getByText(/Temporary mesh/).count(), 0, 'the final mesh is no temporary mesh')
  // Another plate can be chosen here, and the choice stays while the question refreshes; the plate of the working
  // mesh is chosen again.
  const other = platesNow.find(p => p.id !== preselected)
  if (other) {
    await plateChoice.selectOption(other.id)
    await page.waitForTimeout(1200)
    assert.equal(await plateChoice.inputValue(), other.id, 'the chosen plate stays while the question refreshes')
    await plateChoice.selectOption(preselected)
  }
  const evaluated = await card.locator('[data-screw-zheight="FR"]').innerText()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  for (let i = 0; i < 20 && calibrations.length < 2; i++) await page.waitForTimeout(250)
  assert.equal(calibrations.length, 2, 'the final mesh is a second calibration')
  // Confirmed: on to the Mesh tab, where the final mesh is shown afterwards.
  await page.waitForFunction(() => location.hash === '#bed', null, { timeout: 5000 })
  await page.locator('[data-mesh-status] [data-calibration-bar]').waitFor({ timeout: 10000 })
  // Back in Bed alignment while the final mesh runs (the console works on its steps): the warning is done, the
  // evaluation of the last screw measurement stays.
  await page.evaluate(() => (location.hash = '#bed/align'))
  await card.waitFor()
  await page.waitForTimeout(3000)
  assert.equal(await stale.count(), 0, 'the final mesh runs: no warning')
  assert.equal(await card.locator('[data-screw-zheight="FR"]').innerText(), evaluated, 'the table keeps the last screw measurement during the final mesh')
  assert.equal(await card.getByText('No measurement', { exact: true }).count(), 0, 'no "No measurement" during the final mesh')
  // The user waits in the Mesh tab; the console's last job is the calibration (as on the printer).
  await page.evaluate(() => (location.hash = '#bed'))
  await page.locator('[role="tablist"]').waitFor()
  assert.ok(calibrations[0].endsWith(`\n${preselected}\nwork`), 'the working mesh went to the preselected plate')
  assert.ok(calibrations[1].endsWith(`\n${preselected}`), 'the final mesh goes to the plate chosen in the question, stored')
  assert.match(calibrations[1], new RegExp(`^[AB]\\n60\\n${soakMax}\\n`), 'the final mesh is sent with the limit as hold time')
  assert.equal(await page.locator('[data-level-chain-bar]').count(), 0, 'the final mesh starts no chain')
  for (let i = 0; i < 10 && (await plates()).calibration.stage !== 'off'; i++) {
    await step()
    await page.waitForTimeout(300)
  }
  consoleJob = { busy: false, completed: true, success: true, generation: consoleJob.generation + 5, command: 'BED_MESH_CALIBRATE PROFILE=default\n', output: '' }
  await page.waitForTimeout(5000)
  // The final mesh replaces the stored measurement of the plate at 60 °C: the warning and the working mesh are gone,
  // the final mesh is the basis – taken over while the Mesh tab was open.
  const done = await plates()
  assert.equal(done.calibration.result, 'saved')
  assert.equal(done.calibration.work_mesh, null, 'the final mesh ends the working mesh')
  const finalMeasure = done.plates.find(p => p.id === preselected).measures.find(m => m.id === done.calibration.measure)
  assert.ok(finalMeasure && finalMeasure.temp === 60 && finalMeasure.measured >= work.measured, 'the stored measurement is replaced')
  const baseAway = await page.evaluate(() => JSON.parse(localStorage.getItem('cc2-level-base') || '{}').choice)
  assert.equal(baseAway, `measure:${finalMeasure.id}`, 'the final mesh became the basis while the Mesh tab was open')
  await page.evaluate(() => (location.hash = '#bed/align'))
  await card.waitFor()
  await page.waitForTimeout(3000)
  assert.equal(await stale.count(), 0, 'the warning is gone')
  assert.equal(await page.locator('[data-level-base]').inputValue(), `measure:${finalMeasure.id}`, 'the final mesh is the basis')
  assert.equal(await page.locator('[data-level-base] option[value="work"]').count(), 0, 'the temporary mesh is gone')
  assert.match((await card.locator('[data-screw-zheight="FR"]').innerText()).replace(/\u00a0/g, ' '), /^[−-]?\d+(\.\d+)? (µm|mm)$/, 'the table stays filled, evaluated with the final mesh')
  assert.equal(actions.length, 2, 'no screw measurement after the final mesh')
  history = null
  assert.deepEqual(errors, [])
  console.log('PASS guided with a new mesh: one working mesh (library unchanged), then the screw points by themselves (retried quietly), carried mesh marked as calculated, warning only once the screws were turned, final mesh asked while in tolerance (no "Not now", also after a reload) and stored')

  // The chain does not depend on the Bed alignment tab: it survives leaving the tab, a reload, a printer that stays busy
  // for minutes, and ends with a lasting note (with a start button) when it cannot start the screw points itself.
  const failures = []
  const chainCase = async (name, fn) => {
    // Every case starts from an idle calibration, no chain in this browser and a printer that accepts.
    for (let i = 0; i < 10 && (await plates()).calibration.stage !== 'off'; i++) await step()
    refuse = 0
    refuseUntil = 0
    refuseWith = null
    consoleJob = { ...consoleJob, busy: false, completed: false, generation: consoleJob.generation + 1, command: '', output: '' }
    await page.evaluate(() => {
      try {
        sessionStorage.clear()
      } catch {}
    })
    await page.goto(`${origin}/#bed/align`)
    await page.reload()
    await openAlign()
    try {
      await fn()
      console.log(`PASS (${name})`)
    } catch (e) {
      failures.push(name)
      console.log(`FAIL (${name}): ${e.message.split('\n')[0]}`)
    }
  }
  // `pick`: "Quick" with the tick "With mesh"; without it the tab keeps what it opened with.
  const openAlign = async (pick = true) => {
    await card.waitFor()
    for (const n of [1, 2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
    if (pick) {
      await page.locator('[data-level-start-mode-button="quick"]').click()
      await tick(true)
    }
  }
  // The tab opened with the tick "With mesh", and the evaluation uses the working mesh (the mesh views are there).
  const evaluatesWithWork = async () => {
    assert.equal(await meshTick.isChecked(), true, 'the tab opens with "With mesh" ticked')
    assert.equal(await page.locator('[data-level-base]').inputValue(), 'work', 'the working mesh is the basis')
    await card.locator('[data-level-view-toggle]').waitFor({ timeout: 5000 })
  }
  // The way remembered in this browser is set to "Quick": the tick decides about the mesh, not the way.
  const rememberQuick = () =>
    page.evaluate(() => {
      try {
        localStorage.setItem('cc2-level-mode', 'quick')
      } catch {}
    })
  const startGuided = async () => {
    await guided.click()
    await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
    await dialog.waitFor({ state: 'detached' })
    await page.locator('[data-level-chain-bar="mesh"]').waitFor({ timeout: 10000 })
  }
  const toWork = async () => {
    for (let i = 0; i < 10 && (await plates()).calibration.stage !== 'off'; i++) {
      await step()
      await page.waitForTimeout(300)
    }
    assert.equal((await plates()).calibration.result, 'work')
  }
  const waitActions = async (n, ms) => {
    for (let i = 0; i < ms / 250 && actions.length < n; i++) await page.waitForTimeout(250)
  }

  // (a) The tab is left while the mesh is measured (another page); the working mesh is measured meanwhile: the screw
  // points start anyway, without coming back, and when the tab is opened again it evaluates with the working mesh,
  // without choosing "With mesh" again.
  await chainCase('a: tab left during the mesh', async () => {
    await startGuided()
    const before = actions.length
    await page.evaluate(() => {
      location.hash = '#dashboard'
    })
    await page.locator('[data-level-chain-bar="mesh"]').waitFor({ timeout: 8000 })
    await toWork()
    await waitActions(before + 1, 20000)
    assert.equal(actions.length, before + 1, 'the screw points start while another page is open')
    assert.equal(actions.at(-1), 'screws:measure:60:2')
    await rememberQuick()
    await page.evaluate(() => {
      location.hash = '#bed/align'
    })
    await openAlign(false)
    await evaluatesWithWork()
    assert.equal(await page.locator('[data-level-chain-bar]').count(), 0)
  })

  // (b) The page is reloaded while the mesh is measured: the chain goes on after the load and starts exactly the promised
  // measurement (here 65 °C, although the reloaded page itself would use 60 °C); the reloaded tab evaluates with the
  // working mesh without choosing "With mesh" again.
  await chainCase('b: reload during the mesh', async () => {
    await page.locator('[data-level-base]').selectOption('custom')
    await page.locator('[data-level-custom-temp]').fill('65')
    await startGuided()
    await step()
    const before = actions.length
    await rememberQuick()
    // Even an untick remembered in this browser does not drop the mesh while the chain runs.
    await page.evaluate(() => localStorage.setItem('cc2-level-mesh', '0'))
    await page.reload()
    await page.locator('[data-level-chain-bar="mesh"]').waitFor({ timeout: 10000 })
    await openAlign(false)
    assert.equal(await meshTick.isChecked(), true, 'the chain runs: "With mesh" stays ticked')
    assert.equal(await meshTick.isDisabled(), true, 'and cannot be unticked while the chain runs')
    await toWork()
    await waitActions(before + 1, 20000)
    assert.equal(actions.length, before + 1, 'the chain goes on after the reload')
    assert.equal(actions.at(-1), 'screws:measure:65:2', 'the promised measurement: 65 °C, 2 × 3 probes')
    await evaluatesWithWork()
    // Reloaded again after the chain, with the working mesh as the basis: "With mesh" again.
    await keepBase(true)
    await rememberQuick()
    await page.reload()
    await openAlign(false)
    await evaluatesWithWork()
    await keepBase(false)
  })

  // (c) The printer refuses the start for more than 60 s after the save (busy): the page keeps trying and shows why.
  await chainCase('c: printer busy for more than 60 s', async () => {
    await startGuided()
    const before = actions.length
    refuseUntil = Date.now() + 1e9
    refuseWith = { status: 409, error: 'Plate verification is still running' }
    await toWork()
    await page.waitForTimeout(15000)
    await page.locator('[data-level-chain-bar="screws"]').getByText('Mesh measured. The printer is still busy, next try in 5 s.').waitFor({ timeout: 5000 })
    await page.locator('[data-level-chain-bar="screws"] [data-level-chain-cancel]').getByText('Do not measure the screws').waitFor()
    refuseUntil = Date.now() + 52000
    await page.waitForTimeout(52000)
    const tries = actionTimes.slice(before)
    assert.ok(tries.length >= 10, `tried again and again (${tries.length} tries)`)
    assert.ok(tries.at(-1) - tries[0] > 60000, `still trying more than 60 s after the first refusal (${Math.round((tries.at(-1) - tries[0]) / 1000)} s)`)
    await page.locator('[data-level-chain-bar="screws"]').waitFor({ timeout: 1000 })
    await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 15000 })
    assert.equal(await page.locator('[data-level-chain-hint]').count(), 0, 'accepted: no note')
  })

  // (d) A refusal that does not go away by itself: no further tries, a lasting note with a button that starts the four
  // screw points. The note goes away once a measurement runs.
  await chainCase('d: lasting note with a start button', async () => {
    await startGuided()
    const before = actions.length
    refuseUntil = Date.now() + 1e9
    refuseWith = { status: 400, error: 'Screw measurement bed target must be a whole number from 0 to 110 C' }
    await toWork()
    const hint = page.locator('[data-level-chain-hint]')
    await hint.waitFor({ timeout: 15000 })
    await page.waitForTimeout(8000)
    assert.equal(await hint.count(), 1, 'the note stays')
    assert.equal(actions.length - before, 1, 'a lasting refusal is not tried again')
    await hint.getByText('Screw measurement bed target', { exact: false }).waitFor()
    // The note keeps its orange; its way back to Bed alignment is orange too, beside its other buttons, not on that tab.
    await page.evaluate(() => (location.hash = '#dashboard'))
    const back = hint.locator('[data-mesh-status-open="align"]')
    await back.waitFor({ timeout: 5000 })
    assert.match(await back.getAttribute('class'), /border-amber/, 'the button of the note is orange')
    await hint.locator('[data-level-chain-start]').waitFor()
    await hint.locator('[data-level-chain-dismiss]').waitFor()
    await back.click()
    await page.waitForFunction(() => location.hash === '#bed/align')
    await back.waitFor({ state: 'detached' })
    refuseUntil = 0
    await hint.locator('[data-level-chain-start]').click()
    await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
    await waitActions(before + 2, 8000)
    assert.equal(actions.at(-1), 'screws:measure:60:2', 'the button starts the four screw points')
    await hint.waitFor({ state: 'detached', timeout: 10000 })
  })

  // (e) The note also goes away when the user starts a measurement himself; the follow-up can be called off.
  await chainCase('e: own measurement and calling the follow-up off', async () => {
    await startGuided()
    refuseUntil = Date.now() + 1e9
    refuseWith = { status: 400, error: 'Screw measurement bed target must be a whole number from 0 to 110 C' }
    await toWork()
    const hint = page.locator('[data-level-chain-hint]')
    await hint.waitFor({ timeout: 15000 })
    consoleJob = { ...consoleJob, busy: true, completed: false, generation: consoleJob.generation + 1, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n', output: '' }
    await hint.waitFor({ state: 'detached', timeout: 10000 })
    consoleJob = { ...consoleJob, busy: false, completed: true, success: false }
    await page.waitForTimeout(3000)
    refuseUntil = 0
    // Called off while the screws wait for the printer: nothing is started afterwards.
    refuseUntil = Date.now() + 1e9
    refuseWith = { status: 409, error: 'Screw measurement requires the printer to be idle' }
    await startGuided()
    await toWork()
    const bar = page.locator('[data-level-chain-bar="screws"]')
    await bar.waitFor({ timeout: 10000 })
    await bar.locator('[data-level-chain-cancel]').click()
    await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 5000 })
    const tried = actions.length
    refuseUntil = 0
    await page.waitForTimeout(8000)
    assert.equal(actions.length, tried, 'a called-off follow-up starts nothing')
    assert.equal(await page.locator('[data-level-chain-hint]').count(), 0)
  })

  // (f) Busy for longer than the limit (5 min): the chain gives up with the lasting note and the last reason.
  await chainCase('f: busy beyond the limit', async () => {
    await startGuided()
    refuseUntil = Date.now() + 1e9
    refuseWith = { status: 409, error: 'Screw measurement requires the printer to be idle' }
    await toWork()
    await page.locator('[data-level-chain-bar="screws"]').waitFor({ timeout: 10000 })
    await page.waitForTimeout(6000)
    // Five minutes are not waited for: the remembered first try is moved back, as if they had passed.
    const moved = await page.evaluate(() => {
      const raw = sessionStorage.getItem('cc2-level-chain')
      if (!raw) return false
      const c = JSON.parse(raw)
      c.firstTry -= 6 * 60 * 1000
      sessionStorage.setItem('cc2-level-chain', JSON.stringify(c))
      return true
    })
    assert.ok(moved, 'the chain is kept in this browser tab')
    await page.reload()
    const hint = page.locator('[data-level-chain-hint]')
    await hint.waitFor({ timeout: 15000 })
    await hint.getByText('Screw measurement requires the printer to be idle', { exact: false }).waitFor()
    await hint.locator('[data-level-chain-start]').waitFor()
  })

  // (g) CC2 Control restarts while the printer is still busy: the working mesh is gone, so the screw points are not
  // started (they would be evaluated with another basis); a lasting note says so, without a start button.
  await chainCase('g: working mesh lost before the screws', async () => {
    await startGuided()
    const before = actions.length
    refuseUntil = Date.now() + 1e9
    refuseWith = { status: 409, error: 'Screw measurement requires the printer to be idle' }
    await toWork()
    await page.locator('[data-level-chain-bar="screws"]').waitFor({ timeout: 10000 })
    forgetWork = true
    const hint = page.locator('[data-level-chain-hint="lost"]')
    await hint.getByText('The temporary mesh is gone (CC2 Control restarted, another mesh measured, or the plate deleted). The screws were not measured.').waitFor({ timeout: 15000 })
    assert.equal(await hint.locator('[data-level-chain-start]').count(), 0, 'no start button without the working mesh')
    const tried = actions.length
    refuseUntil = 0
    await page.waitForTimeout(7000)
    assert.equal(actions.length, tried, 'nothing is started after the working mesh is gone')
    assert.ok(tried - before <= 2, 'at most the refused tries before it was gone')
    forgetWork = false
    await hint.locator('[data-level-chain-dismiss]').click()
  })

  // (h) The mesh of the chain is stopped from the calibration bar, here on the tab of the plates: the calibration ends as
  // "cancelled", the chain says so (no page of its own does there) and its bar goes away without a note, no screw
  // measurement is started, the working mesh of the run before stays, and the normal measurement buttons are free again.
  await chainCase('h: stop during the mesh of the chain', async () => {
    await startGuided()
    const before = actions.length
    const startedCalibrations = calibrations.length
    assert.equal(await card.locator('[data-level-chain]').count(), 0, 'no chain line in the tab')
    await page.evaluate(() => {
      location.hash = '#bed/plates'
    })
    await page.locator('[data-calibration-stop]').click()
    for (let i = 0; i < 20 && (await plates()).calibration.stage !== 'off'; i++) await page.waitForTimeout(250)
    const stopped = await plates()
    assert.equal(stopped.calibration.result, 'cancelled', 'the calibration ends as cancelled')
    assert.ok(stopped.calibration.work_mesh, 'a calibration stopped before its probing keeps the working mesh')
    await page.getByText('The calibration was stopped and the bed heater switched off.').waitFor({ timeout: 5000 })
    await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 10000 })
    await page.waitForTimeout(3000)
    assert.equal(await page.locator('[data-level-chain-hint]').count(), 0, 'no note after a stop by the user')
    assert.equal(await page.locator('[data-level-chain-bar]').count(), 0, 'the bar stays away')
    assert.equal(actions.length, before, 'no screw measurement is started')
    assert.equal(calibrations.length, startedCalibrations, 'no other calibration is started')
    await page.evaluate(() => {
      location.hash = '#bed/align'
    })
    await openAlign(false)
    assert.equal(await guided.isDisabled(), false, 'the start buttons are free again')
  })

  // (j) After an evaluated measurement step 2 has "Measure the screws again": the same action and question as the start
  // of step 1 (here "Quick" with the temporary mesh); in a screw session it is not there (the session has its own buttons).
  await chainCase('j: measure the screws again in step 2', async () => {
    const heights = { '27.9,25.95': -0.655, '227.15,25.95': -0.868, '227.15,225.5': -0.56, '27.9,225.5': -0.69 }
    const before = actions.length
    await startGuided()
    await toWork()
    await waitActions(before + 1, 15000)
    consoleJob = job(consoleJob.generation + 1, heights)
    await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 15000 })
    const again = page.locator('[data-level-step="2"] [data-level-measure-again]')
    await again.getByText('Measure the screws again', { exact: true }).waitFor({ timeout: 10000 })
    const first = page.locator('[data-level-start-button="quick"]')
    await first.click()
    const questionFirst = await dialog.locator('[data-confirm-text]').innerText()
    await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
    await waitActions(before + 2, 8000)
    const fromStep1 = actions.at(-1)
    consoleJob = job(consoleJob.generation + 1, heights)
    await page.waitForFunction(() => !document.querySelector('[data-level-measure-again]')?.disabled, null, { timeout: 15000 })
    await again.click()
    assert.equal(await dialog.locator('[data-confirm-text]').innerText(), questionFirst, 'the same question')
    await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
    await waitActions(before + 3, 8000)
    assert.equal(actions.at(-1), fromStep1, 'the same action as the start in step 1')
    consoleJob = job(consoleJob.generation + 1, heights)
    await page.route('**/api/printer', async route => {
      const response = await route.fetch(), data = await response.json()
      data.screws_session = { active: true, xy_homed: true, bed: 60 }
      await route.fulfill({ response, json: data })
    })
    await page.locator('[data-level-session-bar]').waitFor({ timeout: 15000 })
    assert.equal(await again.count(), 0, 'not in a screw session')
    await page.unroute('**/api/printer')
  })

  // (i) The way and the tick chosen last are kept in this browser, also with a saved mesh as the basis.
  await chainCase('i: way and tick survive a reload', async () => {
    await keepBase(true)
    const saved = await page.locator('[data-level-base] option[value^="measure:"]').first().getAttribute('value')
    await page.locator('[data-level-base]').selectOption(saved)
    await page.locator('[data-level-start-mode-button="session"]').click()
    await page.reload()
    await openAlign(false)
    assert.equal(await page.locator('[data-level-start-mode-button="session"]').getAttribute('aria-pressed'), 'true', '"With session" stays')
    assert.equal(await meshTick.isChecked(), true, '"With mesh" stays ticked')
    assert.equal(await page.locator('[data-level-base]').inputValue(), saved)
    await card.locator('[data-level-view-toggle]').waitFor({ timeout: 5000 })
    await page.locator('[data-level-start-mode-button="quick"]').click()
    await keepBase(false)
  })

  assert.deepEqual(errors, [])
  if (failures.length) {
    console.log(`FAIL chain cases: ${failures.join(', ')}`)
    process.exitCode = 1
  } else console.log('PASS the chain survives leaving the tab, a reload and a busy printer, and ends with a lasting note')
} finally {
  await browser?.close()
  await server.close()
}
