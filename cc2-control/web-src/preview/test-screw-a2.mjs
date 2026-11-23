// Screw page, final table (A2): Position · Measured · Target · Adjustment, the unit right behind every height,
// micrometres by default, millimetres on request (kept in this browser), the target of the combined plan.
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { heightLabel, parseHeightUnit, screwPlan, screwTarget } from '../src/lib/screws.ts'

// The unit text: whole micrometres or millimetres with three decimals, the unit behind the number (no-break space).
assert.equal(heightLabel(-0.868, 'um'), '−868 µm')
assert.equal(heightLabel(-0.868, 'mm'), '−0.868 mm')
assert.equal(heightLabel(0.05, 'um', true), '+50 µm')
assert.equal(heightLabel(0.05, 'mm', true), '+0.050 mm')
assert.equal(heightLabel(0, 'um', true), '0 µm')
assert.equal(heightLabel(-0.0004, 'mm', true), '0.000 mm', 'a value that rounds to zero has no sign')
assert.equal(heightLabel(-0.0004, 'um'), '0 µm')
assert.equal(parseHeightUnit('mm'), 'mm')
assert.equal(parseHeightUnit('um'), 'um')
assert.equal(parseHeightUnit(null), 'um', 'micrometres unless millimetres was chosen')
assert.equal(parseHeightUnit('nm'), 'um')
// The target: the front-left height; the combined plan moves it by the common shift.
const plain = { '27.9,25.95': -0.7, '227.15,25.95': -0.62, '227.15,225.5': -0.8, '27.9,225.5': -0.75 }
assert.equal(screwTarget(plain, screwPlan(plain)), -0.7)
const lowAll = { '27.9,25.95': -0.7, '227.15,25.95': -0.8, '227.15,225.5': -0.85, '27.9,225.5': -0.9 }
assert.ok(screwPlan(lowAll).canOptimize)
assert.equal(screwPlan(lowAll).useOptimized, false, 'offered, not used until chosen')
assert.equal(screwTarget(lowAll, screwPlan(lowAll)), -0.7, 'standard aims at front left')
assert.ok(Math.abs(screwTarget(lowAll, screwPlan(lowAll, 'optimized')) - -0.8) < 1e-9, 'the combined plan aims at front left + common')

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
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
const shots = path.join(root, 'preview/artifacts')
const errors = []
let browser
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
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  await page.addInitScript(() => { try { localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' })) } catch {} })
  page.on('pageerror', e => errors.push(e.message))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  let consoleJob = job(60, plain)
  await page.route(/\/api\/console(\?.*)?$/, route =>
    route.request().method() === 'GET' ? route.fulfill({ json: consoleJob }) : route.fallback()
  )
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.evaluate(() => {
    for (const k of ['cc2-screw-height-unit', 'cc2-screw-turn-mode', 'cc2-screw-turn-unit']) localStorage.removeItem(k)
  })
  await page.goto(`${origin}/#bed/screws`)
  const card = page.locator('#screwFocus')
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  const ROW_IDS = { 'Front left': 'FL', 'Front right': 'FR', 'Back right': 'RR', 'Back left': 'RL', 'Vorne links': 'FL', 'Vorne rechts': 'FR', 'Hinten rechts': 'RR', 'Hinten links': 'RL' }
  // The row of a corner (its first cell also holds the number button 1–4).
  const row = name => card.locator(`tbody tr[data-screw-row="${ROW_IDS[name]}"]`)
  const cell = (id, what) => card.locator(`[data-screw-${what}="${id}"]`)
  const text = async loc => (await loc.innerText()).replace(/ /g, ' ').trim()

  // The three lists in the accepted order, the line about the viewing direction, the columns, the defaults.
  await row('Front right').waitFor({ timeout: 8000 })
  const lists = card.locator('[data-screw-lists] label')
  assert.deepEqual((await lists.allInnerTexts()).map(x => x.split('\n')[0].trim()), ['Mode', 'Heights in', 'Turn in'])
  assert.deepEqual(
    await card.locator('[data-screw-lists] select').evaluateAll(es => es.map(e => e.value)),
    ['auto', 'um', 'degrees'],
    'Auto, micrometres, degrees by default'
  )
  await card.locator('[data-screw-from-below]').getByText('Turns seen from below onto the screw.', { exact: true }).waitFor()
  const heads = (await card.locator('[data-screw-proposal-block] thead th').allInnerTexts()).map(x => x.trim())
  assert.deepEqual(heads, ['Position', 'Target', 'Direction & amount', 'Turn aid'], 'the table is the suggestion; what was measured stands in its own table above')
  // The Difference column is gone (the amount stands in "Direction & amount"); the target column names what each row aims at.
  assert.equal(await card.locator('[data-screw-diff]').count(), 0, 'no Difference column any more')
  assert.equal(await text(cell('FL', 'goal')), 'Leave unchanged')
  for (const id of ['FR', 'RR', 'RL']) assert.equal(await text(cell(id, 'goal')), 'Bring to the height of front left', `${id}: target`)
  // Measured and target are written relative to the front-left screw (the picture does the same): −700 µm absolute is 0.
  assert.equal(await text(cell('FL', 'measured')), 'Reference')
  assert.equal(await card.locator('[data-screw-measured="FR"], [data-screw-measured="RR"], [data-screw-measured="RL"]').count(), 0, 'only front left has a first line (Reference); the others show no difference')
  // Front left also shows what was really measured (absolute, in the chosen unit), marked as the reference.
  assert.equal(await text(card.locator('[data-screw-zheight="FL"]')), '−700 µm')
  assert.equal(await text(card.locator('[data-screw-zheight="FR"]')), '−620 µm', 'the other corners show their own measured height')
  assert.equal(await text(card.locator('[data-screw-zheight="RR"]')), '−800 µm')
  assert.equal(await card.locator('[data-screw-target]').count(), 0, 'there is no Target column any more')
  // Line 1 of the adjustment: direction and amount as a height; line 2: the symbol shows the direction of the turn.
  assert.equal(await text(cell('FR', 'amount')), '80 µm')
  assert.equal(await text(cell('FR', 'adjust').locator('[data-screw-direction]')), 'Lower 80 µm')
  assert.equal(await cell('FR', 'turn-cell').locator('[data-screw-rotation]').getAttribute('data-screw-rotation'), 'ccw', 'lowering turns counter-clockwise from below')
  assert.equal(await cell('RR', 'turn-cell').locator('[data-screw-rotation]').getAttribute('data-screw-rotation'), 'cw', 'raising turns clockwise from below')
  assert.equal(await text(cell('RR', 'amount')), '100 µm')
  // Auto (default): what can really be turned, as the reading aid: whole clicks, quarters first, the rest in µm.
  // FR is 80 µm high: lower it, 80 / (0.7 mm per turn) = 41.1°, whole clicks 8 = 40°, the rest is named.
  assert.equal(await text(cell('FR', 'aid')), '40° · remaining −2 µm', 'Auto in degrees: 8 clicks = 40°, with the rest')
  assert.equal(await cell('FR', 'exact').count(), 0, 'Auto shows no exact value')
  assert.equal(await cell('FR', 'adjust').locator('[data-screw-direction]').getAttribute('data-screw-direction'), 'lower')
  await row('Front right').getByText('lower', { exact: false }).first().waitFor()
  assert.equal(await text(cell('RR', 'aid')), '50° · remaining +3 µm')
  assert.equal(await cell('RR', 'adjust').locator('[data-screw-direction]').getAttribute('data-screw-direction'), 'raise')
  assert.equal(await text(cell('RL', 'aid')), '25° · remaining +1 µm')
  assert.equal(await text(cell('FL', 'adjust')), 'No turn', 'the reference row is left as it is: direction cell "No turn" (no dash), target cell "Leave unchanged"')
  assert.equal(await cell('FL', 'aid').count(), 0)
  assert.equal(await row('Front left').getByText('Leave the screw').count(), 0)
  await page.screenshot({ path: path.join(shots, 'screw-a2-um-1440.png'), fullPage: true })

  // The unit of the turn works in Exact: every unit from the same exact angle, one decimal, the angle in brackets.
  const mode = card.locator('[data-screw-mode] select')
  assert.deepEqual(await mode.locator('option').evaluateAll(os => os.map(o => o.value)), ['auto', 'exact'], 'only Auto and Exact')
  await mode.selectOption('exact')
  const turnUnit = card.locator('[data-screw-turn-unit] select')
  assert.equal(await text(cell('FR', 'exact')), '41.1°')
  assert.equal(await text(cell('RR', 'exact')), '51.4°')
  assert.equal(await text(cell('RL', 'exact')), '25.7°')
  assert.equal(await cell('FR', 'aid').count(), 0, 'Exact shows no reading aid')
  assert.equal(await cell('FL', 'exact').count(), 0)
  assert.equal(await cell('RR', 'turn-cell').locator('[data-screw-rotation]').getAttribute('data-screw-rotation'), 'cw', 'the turn symbol stays in Exact')
  await turnUnit.selectOption('clicks')
  assert.equal(await text(cell('FR', 'exact')), '8.2 clicks')
  await turnUnit.selectOption('fractions')
  assert.equal(await text(cell('FR', 'exact')), '0.114 turn (≈ 1/12)')
  await turnUnit.selectOption('clock')
  assert.equal(await text(cell('FR', 'exact')), '−7 min', 'the clock in minutes: a minus sign against the clock (FR is lowered, ↺; 41.1° = 6.9 min)')
  assert.equal(await text(cell('RR', 'exact')), '+9 min', 'a plus sign with the clock (RR is raised, ↻; 51.4° = 8.6 min)')
  await turnUnit.selectOption('clockhours')
  assert.equal(await text(cell('FR', 'exact')), 'from 12 to 10:45', 'the clock in hours against the clock: the handle runs back from 12 (41.1° = 5 quarter hours)')
  assert.equal(await text(cell('RR', 'exact')), 'from 12 to 1:45', 'with the clock: forwards (51.4° = 7 quarter hours)')
  await turnUnit.selectOption('degrees')
  await mode.selectOption('auto')
  // The unit works in Auto as well: the value rounded to whole clicks, always with the rest in µm.
  assert.equal(await text(cell('FR', 'aid')), '40° · remaining −2 µm')
  await turnUnit.selectOption('clicks')
  assert.equal(await text(cell('FR', 'aid')), '8 clicks · remaining −2 µm')
  assert.equal(await text(cell('RR', 'aid')), '10 clicks · remaining +3 µm')
  await turnUnit.selectOption('clock')
  assert.equal(await text(cell('FR', 'aid')), '−7 min · remaining −2 µm')
  await turnUnit.selectOption('clockhours')
  assert.equal(await text(cell('FR', 'aid')), 'from 12 to 10:45 · remaining −2 µm')
  await turnUnit.selectOption('fractions')
  assert.equal(await text(cell('FR', 'aid')), '0.08 turns (1/12) · remaining −22 µm', 'fractions: clock-grid fractions with the turns as a decimal first, the rest behind')
  await turnUnit.selectOption('degrees')

  // Millimetres: every height changes unit, nothing else; the choices survive a reload.
  await card.locator('[data-screw-unit] select').selectOption('mm')
  assert.equal(await text(cell('FL', 'measured')), 'Reference')
  assert.equal(await text(card.locator('[data-screw-zheight="FL"]')), '−0.700 mm')
  assert.equal(await text(card.locator('[data-screw-zheight="FR"]')), '−0.620 mm')
  assert.equal(await text(cell('FR', 'amount')), '0.080 mm')
  assert.equal(await text(cell('FR', 'aid')), '40° · remaining −0.002 mm')
  await card.getByText('+0.080 mm').first().waitFor() // the picture follows the unit
  assert.equal(await card.getByText('+80 µm').count(), 0, 'no micrometre left over in the picture')
  await mode.selectOption('exact')
  await turnUnit.selectOption('clicks')
  await page.screenshot({ path: path.join(shots, 'screw-a2-mm-1440.png'), fullPage: true })
  await page.reload()
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  await cell('FL', 'measured').waitFor({ timeout: 8000 })
  assert.deepEqual(
    await card.locator('[data-screw-lists] select').evaluateAll(es => es.map(e => e.value)),
    ['exact', 'mm', 'clicks'],
    'the choices are kept'
  )
  assert.equal(await text(cell('FL', 'measured')), 'Reference')
  // An old stored mode (coarse, fine) is read as Auto.
  await page.evaluate(() => localStorage.setItem('cc2-screw-turn-mode', 'fine'))
  await page.reload()
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  assert.equal(await mode.inputValue(), 'auto', 'a stored "fine" becomes Auto')
  await page.evaluate(() => localStorage.setItem('cc2-screw-turn-mode', 'coarse'))
  await page.reload()
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  assert.equal(await mode.inputValue(), 'auto', 'a stored "coarse" becomes Auto')
  await mode.selectOption('auto')
  await turnUnit.selectOption('degrees')

  // Close enough: the text names the tolerance in the unit.
  consoleJob = job(61, { '27.9,25.95': -0.7, '227.15,25.95': -0.69, '227.15,225.5': -0.71, '27.9,225.5': -0.8 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await cell('FR', 'adjust').locator('[data-screw-ok]').getByText('Within tolerance', { exact: true }).waitFor({ timeout: 8000 })
  // Within the tolerance the small adjustment that is still possible stays visible: the user decides.
  assert.match(await text(card.locator('[data-screw-possible="FR"]')), /^Still possible: Lower \S+ (mm|µm) · ↺ /)
  assert.equal(await card.locator('[data-screw-carrier-tilt]').count(), 0, 'no carrier note for a small difference')
  assert.equal(await cell('FR', 'aid').count() + (await cell('FR', 'exact').count()), 0, 'inside the tolerance there is nothing to turn')
  await card.locator('[data-screw-unit] select').selectOption('um')
  assert.equal(await text(card.locator('[data-screw-zheight="FL"]')), '−700 µm', 'the unit follows the choice')
  // The clock in hours for a very small turn: no "from 12 to 12:00"; against the clock the hand runs backwards
  // (FR 3 µm higher: lower, ↺, 1.5° rounds to no quarter hour; RR 10 µm higher: 5.1° = one quarter hour back).
  consoleJob = job(65, { '27.9,25.95': -0.7, '227.15,25.95': -0.697, '227.15,225.5': -0.69, '27.9,225.5': -0.7 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await card.locator('[data-screw-possible="FR"]').getByText('Lower 3 µm', { exact: false }).waitFor({ timeout: 8000 })
  await turnUnit.selectOption('clockhours')
  assert.match(await text(card.locator('[data-screw-possible="FR"]')), /↺ less than a quarter hour on the clock$/)
  assert.match(await text(card.locator('[data-screw-possible="RR"]')), /↺ from 12 to 11:45$/)
  await turnUnit.selectOption('clock')
  assert.match(await text(card.locator('[data-screw-possible="RR"]')), /↺ −1 min$/, 'the clock in minutes with a minus sign')
  assert.match(await text(card.locator('[data-screw-possible="FR"]')), /↺ less than 1 min$/, 'under half a minute: no 0:00')
  await turnUnit.selectOption('degrees')

  // A large deviation (over half a turn): Auto says coarse first, then measure, then readjust; Exact gives the whole value.
  consoleJob = job(63, { '27.9,25.95': -0.868, '227.15,25.95': -2.155, '227.15,225.5': -0.14, '27.9,225.5': -0.5 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await cell('FR', 'aid').getByText('first 630° · measure · then readjust').waitFor({ timeout: 8000 })
  assert.equal(await cell('FR', 'adjust').locator('[data-screw-direction]').getAttribute('data-screw-direction'), 'raise')
  assert.equal(await text(cell('RR', 'aid')), 'first 360° · measure · then readjust')
  await turnUnit.selectOption('clicks')
  assert.equal(await text(cell('FR', 'aid')), 'first 126 clicks · measure · then readjust', 'the coarse step in the chosen unit')
  await turnUnit.selectOption('clock')
  assert.equal(await text(cell('FR', 'aid')), 'first 1 turn, +45 min · measure · then readjust', 'FR is raised here: with the clock; 630° = 105 min = 1 turn and 45 min')
  await turnUnit.selectOption('degrees')
  await mode.selectOption('exact')
  assert.equal(await text(cell('FR', 'exact')), '661.9°')
  assert.equal(await text(cell('RR', 'exact')), '374.4°')
  await turnUnit.selectOption('clicks')
  assert.equal(await text(cell('FR', 'exact')), '132.4 clicks', 'over half a turn Exact still gives the whole exact value')
  await turnUnit.selectOption('degrees')
  await mode.selectOption('auto')
  await page.screenshot({ path: path.join(shots, 'screw-a2-large-1440.png'), fullPage: true })

  // Back and front more than 1 mm apart: a note points at the bed carrier on its Z axes.
  consoleJob = job(70, { '27.9,25.95': -1.0, '227.15,25.95': -1.06, '227.15,225.5': 0.1, '27.9,225.5': 0.2 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await card.locator('[data-screw-carrier-tilt]').getByText('Z axes', { exact: false }).waitFor({ timeout: 8000 })
  assert.match(await text(card.locator('[data-screw-carrier-tilt]')), /differ by 1[12]\d\d µm \(back higher\)/)

  // A corner the page gave a turn for that hardly moved by the next measurement: its row says to check the sleeve (no
  // call to turn harder, no second note below the table). Once it has moved the note is gone.
  const far = fr => ({ '27.9,25.95': -0.7, '227.15,25.95': fr, '227.15,225.5': -0.7, '27.9,225.5': -0.7 })
  consoleJob = job(80, far(-1.2))
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await cell('FR', 'amount').getByText('500 µm').waitFor({ timeout: 8000 })
  assert.equal(await card.locator('[data-screw-stuck]').count(), 0, 'one measurement alone says nothing')
  consoleJob = job(81, far(-1.19))
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await card.locator('[data-screw-row="FR"] [data-screw-stuck="FR"]').getByText('This corner has hardly moved. Check whether the sleeve turns with the screw.', { exact: true }).waitFor({ timeout: 8000 })
  assert.equal(await card.locator('[data-screw-stuck]').count(), 1, 'only at that corner')
  consoleJob = job(82, far(-0.75))
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await card.locator('[data-screw-stuck]').waitFor({ state: 'detached', timeout: 8000 })

  // A running quick measurement shows the accent-coloured line in the field at the top of the app, as a mesh calibration
  // does; it goes when the measurement is done.
  consoleJob = { busy: true, completed: false, success: false, generation: 71, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: '' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.locator('[data-screw-measure-bar]').getByText('Screw measurement running', { exact: false }).waitFor({ timeout: 10000 })
  assert.match(await page.locator('[data-mesh-status]').getAttribute('class'), /border-cyan/, 'the field has the accent colour of the mesh field')
  assert.doesNotMatch(await page.locator('[data-mesh-status]').getAttribute('class'), /amber/, 'not the orange of the session')
  assert.equal(await page.locator('[data-screw-status]').count(), 0, 'outside a session the tab has no line of its own')
  // The bar offers the cancel only when the printer service says the job can be cancelled; the click sends
  // screws:cancel, the page then says that it stops after the running step, and afterwards that it was cancelled.
  assert.equal(await page.locator('[data-screw-cancel]').count(), 0, 'no cancel button for a job that cannot be cancelled')
  const running = { busy: true, completed: false, success: false, generation: 73, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: '' }
  consoleJob = { ...running, cancellable: true, cancelling: false, cancelled: false }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  const cancel = page.locator('[data-screw-measure-bar] [data-screw-cancel]')
  await cancel.waitFor({ timeout: 10000 })
  assert.equal((await cancel.innerText()).trim(), 'Cancel measurement')
  let cancelPosts = 0
  await page.route('**/api/control', route => {
    if (route.request().method() === 'POST' && route.request().postData() === 'screws:cancel') {
      cancelPosts++
      consoleJob = { ...running, cancellable: true, cancelling: true, cancelled: false }
      return route.fulfill({ status: 202, json: { accepted: true } })
    }
    return route.fallback()
  })
  await cancel.click()
  await page.locator('[data-screw-cancelling]').getByText('Stops as soon as the running step is done', { exact: false }).waitFor({ timeout: 8000 })
  assert.equal(cancelPosts, 1, 'one click sends screws:cancel once')
  assert.equal(await cancel.isDisabled(), true, 'the button is locked while the cancel is pending')
  consoleJob = { ...running, busy: false, completed: true, cancellable: false, cancelling: false, cancelled: true, output: '[CC2 Control: cancelled]\n' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.locator('[data-screw-measure-bar]').waitFor({ state: 'detached', timeout: 20000 })
  await card.locator('[data-screw-cancelled]').getByText('Measurement cancelled: nozzle lifted, heaters off. Nothing was saved.').waitFor({ timeout: 8000 })
  await page.unroute('**/api/control')
  // A measurement of a session shows the same bar with the cancel button (the session's script has its own name).
  consoleJob = { busy: true, completed: false, success: false, generation: 74, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\nG28 Z\n', output: '', cancellable: true, cancelling: false, cancelled: false }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.locator('[data-screw-measure-bar]').getByText('Screw measurement running', { exact: false }).waitFor({ timeout: 10000 })
  await page.locator('[data-screw-measure-bar] [data-screw-cancel]').waitFor()
  assert.equal(await page.locator('[data-screw-cancel]').count(), 1, 'one cancel button, in the bar')
  // The end of a session (heaters off) is no measurement: no bar.
  consoleJob = { busy: true, completed: false, success: false, generation: 75, command: 'M104 S0\nM140 S0', output: '' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.locator('[data-screw-measure-bar]').waitFor({ state: 'detached', timeout: 20000 })
  consoleJob = job(72, { '27.9,25.95': -1.0, '227.15,25.95': -1.06, '227.15,225.5': 0.1, '27.9,225.5': 0.2 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await card.locator('[data-screw-cancelled]').waitFor({ state: 'detached', timeout: 20000 })

  // A printer error ends the measurement (finished, not successful, not cancelled): the page says so and points to the
  // Console page. The message of the printer stays there; the page shows no result of the failed measurement.
  // A lost connection (or any other end without a result that is no printer error) shows no such note.
  consoleJob = { ...job(72, {}), success: false, cancelled: false, output: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nConsole connection ended or command timed out\n' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.waitForTimeout(3500)
  assert.equal(await card.locator('[data-screw-failed]').count(), 0, 'a lost connection is no printer error: no note about one')
  consoleJob = { ...job(73, {}), success: false, cancelled: false, output: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n!! Error: probe failed\n[CC2 Control: the printer reported an error; the job was stopped]\n' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  const failed = card.locator('[data-screw-failed]')
  await failed
    .getByText('The printer reported an error and the measurement was stopped. Details are on the Console page.')
    .waitFor({ timeout: 8000 })
  assert.equal(await card.locator('[data-screw-cancelled]').count(), 0, 'an error is no cancel: one note only')
  // A new result (or a new run) takes the note away again.
  consoleJob = job(74, { '27.9,25.95': -1.0, '227.15,25.95': -1.06, '227.15,225.5': 0.1, '27.9,225.5': 0.2 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await failed.waitFor({ state: 'detached', timeout: 20000 })
  // An error of a command that is no screw measurement does not show the note.
  consoleJob = { busy: false, completed: true, success: false, generation: 75, command: 'G28\n', output: 'Error\n', cancelled: false }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.waitForTimeout(3500)
  assert.equal(await failed.count(), 0, 'only a failed screw measurement shows the note')
  consoleJob = job(76, { '27.9,25.95': -1.0, '227.15,25.95': -1.06, '227.15,225.5': 0.1, '27.9,225.5': 0.2 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now

  // The combined plan: the target is front left plus the common shift, front left moves too; every turned corner,
  // front left included, shows its turn.
  consoleJob = job(62, lowAll)
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  // Standard stays until the operator chooses: front left has nothing to adjust, the optimized plan is only offered.
  await card.locator('[data-screw-optimize-suggest]').waitFor({ timeout: 8000 })
  assert.equal(await text(cell('FL', 'adjust')), 'No turn', 'standard: front left is not adjusted by itself, it is left as it is')
  assert.equal(await text(cell('FL', 'goal')), 'Leave unchanged')
  await card.locator('[data-screw-optimize-choose]').click()
  await cell('FL', 'amount').waitFor({ timeout: 8000 })
  // Front left itself is turned as well: its line 1 names direction and amount (the common shift), the others follow.
  assert.equal(await text(cell('FL', 'adjust').locator('[data-screw-direction]')), 'Lower 100 µm')
  assert.equal(await card.locator('[data-screw-carrier-tilt]').count(), 0, 'the carrier note is gone again')
  assert.equal(await cell('FL', 'turn-cell').locator('[data-screw-rotation]').getAttribute('data-screw-rotation'), 'ccw', 'front left is lowered: turned counter-clockwise')
  assert.ok((await card.locator('[data-screw-rotation]').count()) >= 1, 'the combined plan shows turn symbols')
  assert.equal(await text(cell('FL', 'measured')), 'Reference')
  assert.equal(await cell('FL', 'adjust').locator('[data-screw-direction]').getAttribute('data-screw-direction'), 'lower')
  assert.equal(await text(cell('FL', 'amount')), '100 µm')
  // The targets of the combined plan: front left moves toward the three others, they follow the moved front left.
  assert.equal(await text(cell('FL', 'goal')), 'Move toward the other three corners')
  for (const id of ['FR', 'RR', 'RL']) assert.equal(await text(cell(id, 'goal')), 'Bring to the height of front left (after its move)', `${id}: target in the combined plan`)

  await page.screenshot({ path: path.join(shots, 'screw-a2-combined-1440.png'), fullPage: true })
  // The way back to the standard plan is an explicit button; the rest of this test runs on the standard plan.
  await card.locator('[data-screw-strategy-back]').click()
  await card.locator('[data-screw-strategy="standard"]').waitFor()

  // 390 px, the largest texts (large deviation, Fine mode, Fractions): no horizontal overflow, no cut-off cell.
  consoleJob = job(64, { '27.9,25.95': -0.868, '227.15,25.95': -2.155, '227.15,225.5': -0.14, '27.9,225.5': -0.5 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.setViewportSize({ width: 390, height: 844 })
  await page.reload() // a phone loads the page at its width: the side menu starts closed
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  await card.locator('[data-screw-mode] select').selectOption('exact')
  await cell('FR', 'exact').getByText('661.9°').waitFor({ timeout: 8000 })
  await card.locator('[data-screw-turn-unit] select').selectOption('fractions')
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no overflow at 390 px')
  const cutCells = () =>
    card.locator('[data-level-step="2"] table').evaluateAll(tables =>
      tables.reduce((n, t) => {
        const box = t.getBoundingClientRect()
        return n + [...t.querySelectorAll('th,td')].filter(c => c.scrollWidth > c.clientWidth + 1 || c.getBoundingClientRect().right > box.right + 1).length
      }, 0)
    )
  assert.equal(await cutCells(), 0, 'no table cell is cut off at 390 px (micrometres)')
  await card.locator('[data-screw-unit] select').selectOption('mm')
  assert.equal(await text(cell('FL', 'measured')), 'Reference')
  assert.equal(await text(card.locator('[data-screw-zheight="FR"]')), '−2.155 mm')
  assert.equal(await cutCells(), 0, 'no table cell is cut off at 390 px (millimetres)')
  const wrapped = await card.locator('[data-screw-measured],[data-screw-target]').evaluateAll(es => es.filter(e => e.getBoundingClientRect().height > 20).length)
  assert.equal(wrapped, 0, 'a height with its unit stays on one line')
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no overflow at 390 px (millimetres)')
  await page.screenshot({ path: path.join(shots, 'screw-a2-390.png'), fullPage: true })
  console.log('PASS screw table A2: columns, unit, target, combined plan, 390 px')
} catch (error) {
  console.error(error)
  process.exitCode = 1
} finally {
  await browser?.close()
  await server.close()
}
if (errors.length) {
  console.error(errors)
  process.exitCode = 1
}
