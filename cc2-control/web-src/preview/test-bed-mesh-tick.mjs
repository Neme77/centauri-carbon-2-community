// Bed alignment, step 1: two ways of measuring ("Quick", "With session") and the tick "With mesh" beside them, kept in
// this browser (the earlier third way "With mesh" becomes "Quick" with the tick). With the tick the evaluation takes the
// plate shape of the chosen mesh off for both ways: in a session at the start, after one corner measured again (put into
// the last four heights) and after "Measure again"; the final mesh is asked for there too and says that it ends the
// session. "Measure a new mesh" measures a temporary mesh and then starts the chosen way by itself, without a hold time.
// The hold time of step 1 goes in front of every start the user makes (screws:soak:<min>:…) and stands in the field at
// the top with Stop. A corner that hardly moved after a turn says so in its row. All data is simulated.
// CC2_SHOTS_DIR=<folder>: the pictures of step 1, of a session with a mesh and of the hold time (dark, German, 1440 px).
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS, analyseScrewProbes, fitPlane, gridPoints, meshAt, planeAt } from '../src/lib/mesh-align.ts'
import { SCREW_KEYS, screwPlan, screwsTurnedSince } from '../src/lib/screws.ts'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
const shots = process.env.CC2_SHOTS_DIR || ''
const IDS = ['FL', 'FR', 'RR', 'RL']
const lines = z =>
  IDS.filter(id => z[id] !== undefined)
    .map(id => {
      const s = ALIGN_DEFAULTS.screws.find(x => x.id === id)
      return `${`// probe at ${s.x.toFixed(3)},${s.y.toFixed(3)} is z=${z[id].toFixed(6)}\n`.repeat(3)}// Result is z=${z[id].toFixed(6)}\n`
    })
    .join('')
// A number in µm as the table writes it (the first line: a cell can carry the tolerance "±3 µm" below).
const um = text =>
  Number(text.split(/\n|±/)[0].trim().replace(/\u00a0/g, ' ').replace('Z height: ', '').replace('−', '-').replace('+', '').replace(/ µm$/, ''))
let browser
const errors = []
try {
  await server.listen()
  if (shots) await mkdir(shots, { recursive: true })
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const context = await browser.newContext({ viewport: { width: 1440, height: 1100 } })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  // Every load starts from a typed 60 °C for both choices, unless a case keeps what it chose (cc2-test-keep-base).
  await page.addInitScript(() => {
    try {
      if (!localStorage.getItem('cc2-test-keep-base'))
        localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60', plain: 'custom', plainTemp: '60' }))
    } catch {}
  })
  const now = Math.floor(Date.now() / 1000)
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const response = await route.fetch()
    const lib = await response.json()
    for (const p of lib.plates) for (const m of p.measures) m.measured = now - 3600
    return route.fulfill({ response, json: lib })
  })
  // Every action sent to the printer, in order.
  const actions = []
  page.on('request', r => {
    if (r.method() === 'POST' && new URL(r.url()).pathname === '/api/control') actions.push(r.postData() || '')
  })
  const calibrations = []
  page.on('request', r => {
    if (r.method() === 'POST' && new URL(r.url()).pathname === '/api/plates/calibrate') calibrations.push(r.postData() || '')
  })
  const post = p => page.evaluate(u => fetch(u, { method: 'POST', headers: { 'X-CC2-Request': '1' } }).then(r => r.status), p)

  // (1) An earlier third way "With mesh" becomes "Quick" with the tick.
  await page.addInitScript(() => {
    try {
      if (!sessionStorage.getItem('cc2-test-migrated')) {
        sessionStorage.setItem('cc2-test-migrated', '1')
        localStorage.removeItem('cc2-level-mesh')
        localStorage.setItem('cc2-level-mode', 'mesh')
      }
    } catch {}
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.goto(`${origin}/#bed/align`)
  const card = page.locator('#screwFocus')
  await card.waitFor()
  const unfold = async () => {
    for (const n of [1, 2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
  }
  await unfold()
  const meshTick = card.locator('[data-level-mesh-toggle] input')
  const way = name => card.locator(`[data-level-start-mode-button="${name}"]`)
  assert.equal(await card.locator('[data-level-start-mode-button]').count(), 2, 'two ways of measuring')
  assert.deepEqual(await card.locator('[data-level-start-mode-button]').allInnerTexts(), ['Quick', 'With session'])
  assert.equal(await way('quick').getAttribute('aria-pressed'), 'true', 'the earlier "With mesh" opens as "Quick"')
  assert.equal(await meshTick.isChecked(), true, '… with the tick')
  assert.deepEqual(await page.evaluate(() => [localStorage.getItem('cc2-level-mode'), localStorage.getItem('cc2-level-mesh')]), ['quick', '1'])
  const tick = async on => {
    if ((await meshTick.isChecked()) !== on) await meshTick.click()
    assert.equal(await meshTick.isChecked(), on)
  }
  const soak = card.locator('[data-level-soak]')
  const dialog = page.getByRole('dialog')
  const question = dialog.locator('[data-confirm-text]')
  const confirm = () => dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  const cancel = async () => {
    await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
    await dialog.waitFor({ state: 'detached' })
  }
  const startButton = card.locator('[data-level-start-button]')
  const guided = card.locator('[data-level-guided-new]')

  // (2) Without the tick: the way's start alone, the basis only a temperature (no mesh in the list), no plate shape.
  await tick(false)
  const head = await card.locator('[data-level-start-mode]').boundingBox()
  const tickBox = await card.locator('[data-level-mesh-toggle]').boundingBox()
  assert.ok(tickBox.x >= head.x + head.width && Math.abs(tickBox.y + tickBox.height / 2 - (head.y + head.height / 2)) < 6, 'the tick stands right beside the two ways')
  const base = card.locator('[data-level-base]')
  assert.ok((await base.boundingBox()).y >= head.y + head.height, 'the choice of the basis is below the ways and the tick')
  assert.equal(await base.locator('optgroup[label="Saved meshes"]').count(), 0, 'no mesh to choose without the tick')
  assert.equal(await base.locator('option[value="work"]').count(), 0)
  await card.locator('label', { hasText: 'Temperature as the basis' }).first().waitFor()
  assert.equal(await card.locator('[data-level-age], [data-level-work-note]').count(), 0, 'no mesh note without the tick')
  assert.equal(await guided.count(), 0, 'no "Measure a new mesh" without the tick')
  assert.equal(await startButton.getAttribute('data-level-start-button'), 'quick')
  await startButton.getByText('Measure only (quick)').waitFor()
  // The line under the quick start follows the hold time: "No wait" only without one (2 min is the default).
  assert.equal(await soak.inputValue(), '2')
  await startButton.getByText('Holds the bed 2 min before probing, heaters off afterwards', { exact: true }).waitFor()
  assert.equal(await startButton.getByText('No wait', { exact: false }).count(), 0, 'no "No wait" with a hold time')
  await soak.selectOption('10')
  await startButton.getByText('Holds the bed 10 min before probing, heaters off afterwards', { exact: true }).waitFor()
  await soak.selectOption('0')
  await startButton.getByText('No wait, heaters off afterwards', { exact: true }).waitFor()
  await soak.selectOption('2')
  await way('session').click()
  await startButton.getByText('Start session').waitFor()
  await way('quick').click()

  // (3) With the tick and no mesh chosen: "Measure a new mesh" is the only start.
  await tick(true)
  await card.locator('label', { hasText: 'Mesh or temperature as the basis' }).first().waitFor()
  assert.equal(await startButton.count(), 0, 'no start of the way without a mesh')
  await guided.getByText('Measure a new mesh').waitFor()
  // The line under "Measure a new mesh" says what follows the mesh on the chosen way.
  await guided.getByText('Temporary mesh, then the screw measurement, heaters off afterwards', { exact: true }).waitFor()
  await way('session').click()
  await guided.getByText('Temporary mesh, then the screw measurement, the bed stays on, off after 10 min idle', { exact: true }).waitFor()
  assert.equal(await guided.getByText(/session/i).count(), 0, 'the line does not name the session')
  await way('quick').click()
  await card.locator('[data-level-saved-mesh-off]').getByText('No mesh chosen: choose one above or measure a new mesh.').waitFor()
  // A saved mesh chosen: the way's start, evaluated with it, and "Measure a new mesh" beside it.
  const library = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const mounted = library.plates.find(p => p.id === library.current)
  const reference = mounted.measures.filter(m => m.temp === 60)[0]
  await base.selectOption(`measure:${reference.id}`)
  await startButton.getByText('Evaluated with the saved mesh').waitFor()
  const sb = await startButton.boundingBox()
  const gb = await guided.boundingBox()
  assert.ok(gb.x > sb.x && Math.abs(gb.y - sb.y) < 2, '"Measure a new mesh" stands beside the start')
  // The two choices are kept apart: without the tick the temperature comes back, with it the mesh.
  await tick(false)
  assert.equal(await base.inputValue(), 'custom')
  await tick(true)
  assert.equal(await base.inputValue(), `measure:${reference.id}`)

  // (4) With session and the mesh: every evaluation of the session takes the plate shape off.
  await way('session').click()
  await startButton.getByText('Start session').waitFor()
  await soak.selectOption('0')
  await startButton.click()
  await question.getByText(/Home all axes, heat the bed to 60 °C/).waitFor()
  assert.equal(await question.getByText(/held there for/).count(), 0, 'no hold time: not named')
  await confirm()
  await page.locator('[data-level-session-bar]').waitFor({ timeout: 10000 })
  assert.equal(actions.at(-1), 'screws:tram:start:60:2', 'no hold time: the action alone')
  // The table of the session, as the page shows it: the measured Z heights, the plate shape and what is left.
  const shown = async () => {
    const z = {}
    for (const id of IDS) z[id] = um(await card.locator(`[data-screw-zheight="${id}"]`).innerText()) / 1000
    return z
  }
  const expectWithShape = async what => {
    await card.locator('[data-screw-shape="FR"]').waitFor({ timeout: 10000 })
    const z = await shown()
    const fit = analyseScrewProbes(z, reference.mesh, ALIGN_DEFAULTS)
    assert.ok(fit, `${what}: the mesh can be compared`)
    const plan = screwPlan(Object.fromEntries(SCREW_KEYS.map((k, i) => [k, z[IDS[i]] - fit.shape[i]])))
    for (const [i, id] of IDS.entries()) {
      assert.ok(Math.abs(um(await card.locator(`[data-screw-shape="${id}"]`).innerText()) - Math.round((fit.shape[i] - fit.shape[0]) * 1000)) <= 1, `${what}: plate shape ${id}`)
      if (i) {
        // The plan value of the corner stands in its direction cell (amount, or "Still possible" within the tolerance); the sign
        // is the direction: positive = lower, negative = raise. The status in the measured table says the same thing.
        const amount = card.locator(`[data-screw-amount="${id}"]`)
        const raw = (await amount.count()) ? await amount.innerText() : await card.locator(`[data-screw-possible="${id}"]`).innerText()
        const got = (await amount.count()) ? um(raw) : um(raw.match(/(\d+(?:[.,]\d+)?)\s*µm/)?.[1] ?? '0')
        assert.ok(Math.abs(got - Math.abs(plan.shown[i]) * 1000) <= 1.5, `${what}: plan amount ${id} with the plate shape (${JSON.stringify(raw)} / ${(plan.shown[i] * 1000).toFixed(1)})`)
        const kind = await card.locator(`[data-screw-verdict="${id}"]`).getAttribute('data-screw-verdict-kind')
        assert.equal(kind, Math.abs(plan.shown[i]) <= 0.02 ? 'ok' : plan.shown[i] > 0 ? 'lower' : 'raise', `${what}: status ${id} follows the plan`)
        // The colour of the status is the colour of the direction in the suggestion, with the plate shape too.
        const dirEl = card.locator(`[data-screw-row="${id}"]`).locator(kind === 'ok' ? '[data-screw-ok]' : '[data-screw-direction]')
        const colour = loc => loc.evaluate(e => getComputedStyle(e).color)
        assert.equal(await colour(card.locator(`[data-screw-verdict="${id}"]`)), await colour(dirEl), `${what}: status colour of ${id} is the colour of its direction`)
      }
    }
    return z
  }
  const atStart = await expectWithShape('session start')
  await card.locator('[data-level-mesh-tiles]').waitFor()
  // One corner measured again: put into the last four heights, evaluated with the plate shape again.
  await page.waitForFunction(() => {
    const b = [...document.querySelectorAll('button')].find(x => x.getAttribute('aria-label') === 'Single measurement: Front right')
    return b && !b.disabled
  }, null, { timeout: 15000 })
  const before = actions.length
  await card.getByRole('button', { name: 'Single measurement: Front right' }).click()
  for (let i = 0; i < 20 && actions.length === before; i++) await page.waitForTimeout(100)
  assert.equal(actions.at(-1), 'screws:tram:probe:FR:2', 'in a session the corner has no hold time')
  await page.waitForFunction(was => {
    const t = document.querySelector('[data-screw-zheight="FR"]')?.textContent || ''
    return t && !t.includes(was)
  }, String(Math.round(atStart.FR * 1000)), { timeout: 15000 })
  const afterProbe = await expectWithShape('one corner again')
  assert.equal(afterProbe.FL, atStart.FL, 'the other corners stay as they were')
  assert.equal(afterProbe.RR, atStart.RR)
  // "Measure again": all four, with the plate shape.
  await page.waitForFunction(() => !document.querySelector('[data-screw-check]')?.disabled, null, { timeout: 15000 })
  await card.locator('[data-screw-check]').click()
  await confirm()
  await page.waitForTimeout(3000)
  assert.equal(actions.at(-1), 'screws:tram:check:2')
  await expectWithShape('measure again')
  // The views of the mesh are there in the session too.
  await card.locator('[data-level-view-toggle]').click()
  await card.locator('[data-level-mesh-3d] canvas').waitFor()
  await card.locator('[data-level-carried]').waitFor()
  for (let i = 0; i < 3; i++) await card.locator('[data-level-view-toggle]').click()
  await card.locator('[data-screw-pick-mark="FL"]').waitFor()
  if (shots) await card.locator('[data-level-step="2"]').screenshot({ path: path.join(shots, 'sitzung-mit-mesh-en.png') })

  // In tolerance in the session (the screws were turned since the mesh): the final mesh is asked for, and its question
  // says that it ends the session.
  const plane = fitPlane(gridPoints(reference.mesh, ALIGN_DEFAULTS.row0_is_min_y))
  const onShape = Object.fromEntries(ALIGN_DEFAULTS.screws.map(sc => [sc.id, 1.8 + meshAt(reference.mesh, sc.x, sc.y, ALIGN_DEFAULTS.row0_is_min_y) - planeAt(plane, sc.x, sc.y)]))
  const live = await page.evaluate(async () => (await fetch('/api/screws/history')).json())
  const open = live.sessions.find(s => s.kind !== 'quick' && s.ended == null)
  const turned = { time: now + 5, kind: 'check', corner: null, twice: true, z: onShape }
  assert.ok(screwsTurnedSince([{ ...open, measurements: [...open.measurements, turned] }], reference.measured), 'the history shows a turn')
  await page.route('**/api/screws/history', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const json = await (await route.fetch()).json()
    const s = json.sessions.find(x => x.kind !== 'quick' && x.ended == null)
    if (s) s.measurements.push(turned)
    return route.fulfill({ json })
  })
  await page.route(/\/api\/console(\?.*)?$/, route =>
    route.request().method() === 'GET'
      ? route.fulfill({ json: { busy: false, completed: true, success: true, generation: 500, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\nG28 Z\n', output: `SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n${lines(onShape)}` } })
      : route.fallback()
  )
  const ask = card.locator('[data-level-final-ask]')
  await ask.getByText('All corners are within tolerance. Create the final mesh?').waitFor({ timeout: 15000 })
  assert.equal(await ask.locator('button').count(), 1, 'no "Not now"')
  await ask.locator('[data-level-final-yes]').click()
  await question.getByText(/Measure the final mesh at 60 °C/).waitFor()
  await question.getByText('The running screw session ends first.', { exact: false }).waitFor()
  await cancel()
  assert.equal(calibrations.length, 0, 'declined: nothing is measured')
  // The question stays after a reload.
  await page.evaluate(() => localStorage.setItem('cc2-test-keep-base', '1'))
  await page.reload()
  await card.waitFor()
  await unfold()
  await ask.waitFor({ timeout: 15000 })
  await page.evaluate(() => localStorage.removeItem('cc2-test-keep-base'))
  await page.unroute(/\/api\/console(\?.*)?$/)
  await page.unroute('**/api/screws/history')
  // End the session.
  await page.waitForFunction(() => document.querySelector('[data-session-bar-end]') && !document.querySelector('[data-session-bar-end]').disabled, null, { timeout: 15000 })
  await page.locator('[data-session-bar-end]').click()
  await confirm()
  await page.locator('[data-level-session-bar]').waitFor({ state: 'detached', timeout: 10000 })
  console.log('PASS (4) a session with the mesh: start, one corner and "Measure again" with the plate shape; views; final mesh asked, ends the session, stays after a reload')

  // (5) "Measure a new mesh" with "With session": the temporary mesh, then the session starts by itself, without the
  // hold time of step 1 (that one is the mesh's).
  await page.reload()
  await card.waitFor()
  await unfold()
  await tick(true)
  await way('session').click()
  await soak.selectOption('10')
  await guided.click()
  await question.getByText(/Measure a temporary mesh at 60 °C for/).waitFor()
  await question.getByText('After it the screw session starts with the four screws.', { exact: false }).waitFor()
  await question.getByText('The bed heats to 60 °C and is held there for 10 min before probing.', { exact: false }).waitFor()
  const sent = actions.length
  await confirm()
  await page.locator('[data-level-chain-bar="mesh"]').waitFor({ timeout: 10000 })
  await page.locator('[data-level-chain-bar="mesh"]').getByText('Alignment with a new mesh').waitFor()
  assert.match(calibrations.at(-1), /^[AB]\n60\n10\n[0-9a-f]*\n[0-9a-f]+\nwork$/, 'the mesh takes the hold time')
  for (let i = 0; i < 10 && (await page.evaluate(async () => (await (await fetch('/api/plates')).json()).calibration.stage)) !== 'off'; i++) {
    await post('/__preview/calibration-step')
    await page.waitForTimeout(300)
  }
  await page.locator('[data-level-session-bar]').waitFor({ timeout: 20000 })
  assert.deepEqual(actions.slice(sent), ['screws:tram:start:60:2'], 'the chain starts the session, without a hold time')
  assert.equal((await page.evaluate(async () => (await fetch('/api/printer')).json())).screws_after_mesh, true, 'the preview reports the start right after the mesh')
  await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 10000 })
  assert.equal(await base.inputValue(), 'work', 'evaluated with the temporary mesh')
  await card.locator('[data-screw-shape="FR"]').waitFor({ timeout: 10000 })
  await startButton.getByText('Evaluated with the temporary mesh').waitFor()
  if (shots) await card.locator('[data-level-step="2"]').screenshot({ path: path.join(shots, 'sitzung-mit-mesh-kette-en.png') })
  await page.waitForFunction(() => document.querySelector('[data-session-bar-end]') && !document.querySelector('[data-session-bar-end]').disabled, null, { timeout: 15000 })
  await page.locator('[data-session-bar-end]').click()
  await confirm()
  await page.locator('[data-level-session-bar]').waitFor({ state: 'detached', timeout: 10000 })
  console.log('PASS (5) "Measure a new mesh" then the session, without a hold time')

  // (6) The hold time before the quick measurement: named in the question, sent in front, shown at the top with Stop.
  await page.reload()
  await card.waitFor()
  await unfold()
  await tick(false)
  await way('quick').click()
  await soak.selectOption('10')
  await startButton.click()
  await question.getByText('The bed heats to 60 °C and is held there for 10 min before probing.', { exact: false }).waitFor()
  await confirm()
  await dialog.waitFor({ state: 'detached' })
  for (let i = 0; i < 20 && !actions.at(-1).startsWith('screws:soak'); i++) await page.waitForTimeout(100)
  assert.equal(actions.at(-1), 'screws:soak:10:screws:measure:60:2', 'the hold time in front of the action')
  const field = page.locator('[data-mesh-status]')
  const soakBar = field.locator('[data-screw-soak-bar]')
  await soakBar.waitFor({ timeout: 10000 })
  assert.equal(await soakBar.getAttribute('data-screw-soak-bar'), 'heating')
  await soakBar.getByText('Hold time before the screw measurement').waitFor()
  await soakBar.getByText(/Heating the bed: \d+ °C of 60 °C/).waitFor()
  assert.match(await field.getAttribute('class'), /border-cyan/, 'blue')
  assert.equal(await startButton.isDisabled(), true, 'no other start meanwhile')
  assert.equal(await soakBar.locator('[data-mesh-status-open]').count(), 0, 'no "Bed alignment" on its own tab')
  await page.evaluate(() => (location.hash = '#dashboard'))
  await soakBar.locator('[data-mesh-status-open="align"]').getByText('Bed alignment').waitFor()
  assert.equal(await post('/__preview/screws-soak-step'), 200)
  await soakBar.getByText(/Holding the bed at 60 °C: (10:00|9:[0-5]\d) left/).waitFor({ timeout: 10000 })
  if (shots) await field.screenshot({ path: path.join(shots, 'haltephase-oben-en.png') })
  const history = async () => (await page.evaluate(async () => (await fetch('/api/screws/history')).json())).sessions.length
  const kept = await history()
  await soakBar.locator('[data-screw-soak-stop]').getByText('Stop').click()
  await soakBar.waitFor({ state: 'detached', timeout: 10000 })
  assert.equal(actions.at(-1), 'screws:cancel', 'Stop cancels the hold')
  assert.equal(await history(), kept, 'nothing was measured')
  // A hold that fails: its reason is told once.
  await soakBar.locator('[data-mesh-status-open="align"]').waitFor({ state: 'detached' })
  await page.evaluate(() => (location.hash = '#bed/align'))
  await card.waitFor()
  await soak.selectOption('10')
  await page.waitForFunction(() => !document.querySelector('[data-level-start-button]')?.disabled, null, { timeout: 10000 })
  await startButton.click()
  await confirm()
  await soakBar.waitFor({ timeout: 10000 })
  await page.evaluate(() => fetch('/__preview/screws-soak-step', { method: 'POST', headers: { 'X-CC2-Request': '1' }, body: 'fail' }))
  await soakBar.waitFor({ state: 'detached', timeout: 10000 })
  const told = page.getByText('The hold time before the screw measurement ended: The bed did not reach 60 °C')
  await told.waitFor({ timeout: 10000 })
  await page.waitForTimeout(6000)
  assert.ok((await told.count()) <= 1, 'told once')
  // The whole way: held, then measured.
  await page.waitForFunction(() => !document.querySelector('[data-level-start-button]')?.disabled, null, { timeout: 10000 })
  await startButton.click()
  await confirm()
  await soakBar.waitFor({ timeout: 10000 })
  await post('/__preview/screws-soak-step')
  await post('/__preview/screws-soak-step')
  await soakBar.waitFor({ state: 'detached', timeout: 10000 })
  await card.locator('[data-screw-zheight="FR"]').waitFor({ timeout: 15000 })
  assert.equal(await history(), kept + 1, 'measured after the hold')
  console.log('PASS (6) hold time before the quick measurement: question, prefix, field at the top with Stop, then measured')

  // (7) The hold time before the session start: the field stays blue until the session is open.
  await way('session').click()
  await page.waitForFunction(() => !document.querySelector('[data-level-start-button]')?.disabled, null, { timeout: 10000 })
  await startButton.click()
  await question.getByText('The bed heats to 60 °C and is held there for 10 min before probing.', { exact: false }).waitFor()
  await confirm()
  await soakBar.waitFor({ timeout: 10000 })
  assert.equal(actions.at(-1), 'screws:soak:10:screws:tram:start:60:2')
  await soakBar.getByText('Hold time before the screw session').waitFor()
  assert.equal(await page.locator('[data-level-session-bar]').count(), 0, 'no session yet')
  await post('/__preview/screws-soak-step')
  if (shots) await field.screenshot({ path: path.join(shots, 'haltephase-sitzung-en.png') })
  await post('/__preview/screws-soak-step')
  await page.locator('[data-level-session-bar]').waitFor({ timeout: 10000 })
  await soakBar.waitFor({ state: 'detached', timeout: 10000 })
  await page.waitForFunction(() => document.querySelector('[data-session-bar-end]') && !document.querySelector('[data-session-bar-end]').disabled, null, { timeout: 15000 })
  await page.locator('[data-session-bar-end]').click()
  await confirm()
  await page.locator('[data-level-session-bar]').waitFor({ state: 'detached', timeout: 10000 })
  console.log('PASS (7) hold time before the session start, the session after it')

  // (8) A service that does not know the prefix (400): its text is shown, nothing is sent without it.
  await way('quick').click()
  await page.route('**/api/control', route =>
    (route.request().postData() || '').startsWith('screws:soak:')
      ? route.fulfill({ status: 400, json: { accepted: false, error: 'Unknown screw action' } })
      : route.fallback()
  )
  const tried = actions.length
  await page.waitForFunction(() => !document.querySelector('[data-level-start-button]')?.disabled, null, { timeout: 10000 })
  await startButton.click()
  await confirm()
  await page.getByText('Rejected: Unknown screw action', { exact: false }).waitFor({ timeout: 5000 })
  await page.waitForTimeout(1500)
  assert.deepEqual(actions.slice(tried), ['screws:soak:10:screws:measure:60:2'], 'no quiet second try without the hold time')
  await page.unroute('**/api/control')
  console.log('PASS (8) a refused prefix is shown')

  // (9) A corner the page gave a turn for that hardly moved: the row says so, and no second note stands beside it.
  let job = null
  await page.route(/\/api\/console(\?.*)?$/, route =>
    route.request().method() === 'GET' && job ? route.fulfill({ json: job }) : route.fallback()
  )
  const quickJob = (generation, z) => ({ busy: false, completed: true, success: true, generation, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${lines(z)}` })
  job = quickJob(900, { FL: -0.7, FR: -0.4, RR: -0.71, RL: -0.69 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await card.locator('[data-screw-direction]').first().waitFor({ timeout: 15000 })
  assert.equal(await card.locator('[data-screw-stuck]').count(), 0, 'one measurement alone says nothing')
  job = quickJob(901, { FL: -0.7, FR: -0.41, RR: -0.71, RL: -0.69 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  const stuck = card.locator('[data-screw-row="FR"] [data-screw-stuck="FR"]')
  await stuck.getByText('This corner has hardly moved. Check whether the sleeve turns with the screw.', { exact: true }).waitFor({ timeout: 10000 })
  assert.equal(await card.locator('[data-screw-stuck]').count(), 1, 'only at the corner that was to be turned')
  assert.equal(await card.getByText(/Hardly moved since the last measurement|turn harder|kräftiger/).count(), 0, 'no second note, no call to turn harder')
  job = quickJob(902, { FL: -0.7, FR: -0.6, RR: -0.71, RL: -0.69 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await stuck.waitFor({ state: 'detached', timeout: 10000 })
  // A quick measurement right after a mesh (the service measures without a second nozzle cleaning): the line at the top
  // says so.
  job = { busy: true, completed: false, success: false, generation: 903, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: '' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.route('**/api/printer', async route => {
    const response = await route.fetch()
    const json = await response.json()
    json.screws_after_mesh = true
    return route.fulfill({ response, json })
  })
  await page.locator('[data-screw-measure-bar] [data-screw-after-mesh]').getByText('directly after the mesh', { exact: false }).waitFor({ timeout: 10000 })
  await page.unroute('**/api/printer')
  job = quickJob(904, { FL: -0.7, FR: -0.6, RR: -0.71, RL: -0.69 })
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.locator('[data-screw-measure-bar]').waitFor({ state: 'detached', timeout: 10000 })
  // A quick measurement without the tick stays evaluated while the tick is set and a new mesh runs (the console works on
  // the calibration; only a screw measurement replaces the result).
  await card.locator('[data-screw-zheight="FR"]').waitFor({ timeout: 10000 })
  await tick(true)
  await soak.selectOption('0')
  await guided.click()
  await confirm()
  await page.locator('[data-level-chain-bar="mesh"]').waitFor({ timeout: 10000 })
  job = { busy: true, completed: false, success: false, generation: 905, command: 'BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60', output: '' }
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange'))) // the page looks only every 20 s while nothing runs: look now
  await page.waitForTimeout(4000)
  assert.equal(await card.getByText('No measurement', { exact: true }).count(), 0, 'the measurement stays while the new mesh runs')
  await card.locator('[data-screw-zheight="FR"]').waitFor()
  await page.locator('[data-calibration-stop]').click()
  await page.locator('[data-level-chain-bar]').waitFor({ state: 'detached', timeout: 15000 })
  await tick(false)
  await page.unroute(/\/api\/console(\?.*)?$/)
  console.log('PASS (9) "hardly moved" at the corner; "directly after the mesh" in the line at the top; a measurement stays while a new mesh runs')

  // (10) The pictures: German, dark, 1440 px.
  if (shots) {
    const de = await browser.newContext({ viewport: { width: 1440, height: 1100 } })
    const p = await de.newPage()
    p.on('pageerror', e => errors.push(e.message))
    await p.route('**/api/preferences', route => (route.request().method() === 'GET' ? route.fulfill({ json: { language: 'de', theme: 'dark' } }) : route.fallback()))
    await p.route('**/api/plates', async route => {
      if (route.request().method() !== 'GET') return route.fallback()
      const response = await route.fetch()
      const lib = await response.json()
      for (const pl of lib.plates) for (const m of pl.measures) m.measured = now - 3600
      return route.fulfill({ response, json: lib })
    })
    await p.addInitScript(([id]) => {
      try {
        localStorage.setItem('cc2-level-base', JSON.stringify({ choice: `measure:${id}`, temp: '60', plain: 'material:PLA', plainTemp: '60' }))
        localStorage.setItem('cc2-level-mesh', '0')
        localStorage.setItem('cc2-level-mode', 'quick')
      } catch {}
    }, [reference.id])
    await p.goto(`${origin}/#bed/align`)
    const c = p.locator('#screwFocus')
    await c.waitFor()
    const step1 = c.locator('[data-level-step="1"]')
    await step1.locator('[data-level-mesh-toggle]').waitFor()
    await p.waitForTimeout(800)
    await step1.screenshot({ path: path.join(shots, 'schritt1-haken-aus.png') })
    await step1.locator('[data-level-mesh-toggle] input').click()
    await step1.locator('[data-level-start-button]').waitFor()
    await p.waitForTimeout(500)
    await step1.screenshot({ path: path.join(shots, 'schritt1-haken-an.png') })
    // A session with the mesh: started from step 1 without a hold time.
    await step1.locator('[data-level-start-mode-button="session"]').click()
    await step1.locator('[data-level-soak]').selectOption('0')
    await step1.locator('[data-level-start-button]').click()
    await p.getByRole('dialog').getByRole('button', { name: 'Bestätigen', exact: true }).click()
    await p.locator('[data-level-session-bar]').waitFor({ timeout: 10000 })
    await c.locator('[data-screw-shape="FR"]').waitFor({ timeout: 10000 })
    // Without the toast and the simulation banner: step 2 of the session, with the plate shape.
    await p.getByText('Befehl akzeptiert').waitFor({ state: 'detached', timeout: 15000 }).catch(() => {})
    await p.evaluate(() => {
      const b = document.querySelector('#cc2-preview-banner')
      if (b) b.style.display = 'none'
    })
    await p.waitForTimeout(500)
    await c.locator('[data-level-step="2"]').screenshot({ path: path.join(shots, 'sitzung-mit-mesh.png') })
    await p.locator('[data-session-bar-end]').click()
    await p.getByRole('dialog').getByRole('button', { name: 'Bestätigen', exact: true }).click()
    await p.locator('[data-level-session-bar]').waitFor({ state: 'detached', timeout: 10000 })
    // The hold time at the top.
    await step1.locator('[data-level-start-mode-button="quick"]').click()
    await step1.locator('[data-level-soak]').selectOption('10')
    await p.waitForFunction(() => !document.querySelector('[data-level-start-button]')?.disabled, null, { timeout: 10000 })
    await step1.locator('[data-level-start-button]').click()
    await p.getByRole('dialog').getByRole('button', { name: 'Bestätigen', exact: true }).click()
    await p.locator('[data-screw-soak-bar]').waitFor({ timeout: 10000 })
    await p.evaluate(() => fetch('/__preview/screws-soak-step', { method: 'POST', headers: { 'X-CC2-Request': '1' } }))
    await p.locator('[data-screw-soak-bar="soaking"]').waitFor({ timeout: 10000 })
    await p.waitForTimeout(1200)
    const top = await p.locator('[data-mesh-status]').boundingBox()
    await p.screenshot({ path: path.join(shots, 'haltephase-oben.png'), clip: { x: 0, y: 0, width: 1440, height: Math.ceil(top.y + top.height + 260) } })
    await p.locator('[data-screw-soak-stop]').click()
    await de.close()
  }
  assert.deepEqual(errors, [])
  console.log('PASS bed alignment: two ways and the tick "With mesh", a session with the plate shape, the chain into a session, the hold time for every start, "hardly moved" at the corner')
} finally {
  await browser?.close()
  await server.close()
}
