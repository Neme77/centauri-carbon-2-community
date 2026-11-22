import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import {
  cornerOffsets,
  measurementOffsets,
  offsetChain,
  parseScrewHistory,
  referenceMove,
  screwHistoryCsv,
  screwPlan,
  screwTimeLocal,
} from '../src/lib/screws.ts'

const measure = (partial) => ({ time: 1, kind: 'start', corner: null, twice: false, z: {}, ...partial })
const trail = (measurements) => ({ started: 1, ended: null, measurements })
const twice = referenceMove(
  trail([
    measure({ z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } }),
    measure({ time: 2, kind: 'probe', corner: 'FL', twice: true, z: { FL: 1.802 }, z2: { FL: 1.81 } }),
  ])
)
assert.equal(twice.change, '0 → +6 µm', '2× 3 uses the average of both results (1.802 and 1.810)')
assert.equal(
  referenceMove(trail([measure({ z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } })])),
  null,
  'a single front-left reading shows no reference shift'
)
assert.equal(twice.sinceFullUm, 6)
const shifted = referenceMove(
  trail([
    measure({ z: { FL: 1.8, FR: 1.85, RR: 1.82, RL: 1.79 } }),
    measure({ time: 2, kind: 'probe', corner: 'FL', z: { FL: 1.78 } }),
  ])
)
assert.equal(shifted.change, '0 → −20 µm')
assert.equal(shifted.sinceFullUm, -20)
const checked = referenceMove(
  trail([
    measure({ z: { FL: 1.8 } }),
    measure({ time: 2, kind: 'probe', corner: 'FL', z: { FL: 1.78 } }),
    measure({ time: 3, kind: 'check', z: { FL: 1.779, FR: 1.85, RR: 1.82, RL: 1.79 } }),
  ])
)
assert.equal(checked.sinceFullUm, null, 'a full check clears the stale notice')
assert.equal(checked.change, '0 → −21 µm')
const lowFl = { '27.9,25.95': 1.8, '227.15,25.95': 1.74, '227.15,225.5': 1.72, '27.9,225.5': 1.7 }
assert.equal(screwPlan(lowFl).canOptimize, true, 'the optimized plan is offered')
assert.equal(screwPlan(lowFl).useOptimized, false, 'but not used until it is chosen')
assert.deepEqual(screwPlan(lowFl).shown.map(v => Math.round(v * 1000)), [0, -60, -80, -100], 'standard: front left stays the reference')
const optimized = screwPlan(lowFl, 'optimized')
assert.equal(optimized.useOptimized, true)
assert.deepEqual(
  optimized.shown.map(v => Math.round(v * 1000)),
  [60, 0, -20, -40]
)
// Following the optimised plan must level the bed (positive = lower this corner by that much).
{
  const z = [1.8, 1.74, 1.72, 1.7]
  const after = z.map((v, i) => v - optimized.shown[i])
  assert.ok(Math.max(...after) - Math.min(...after) < 1e-9, `optimised plan levels the bed: ${after}`)
}
// Seen on a printer (#365 draft): front-left lowest, the plan must say raise it.
{
  const z = { '27.9,25.95': -0.843, '227.15,25.95': -0.627, '227.15,225.5': -0.757, '27.9,225.5': -0.746 }
  const p = screwPlan(z, 'optimized')
  const v = Object.values(z)
  const after = v.map((x, i) => x - p.shown[i])
  const spread = l => Math.max(...l) - Math.min(...l)
  assert.ok(spread(after) < spread(v), `the plan does not make it worse: ${after}`)
  if (p.useOptimized) assert.ok(p.shown[0] < 0, 'front-left is the lowest corner, so it is raised')
}
const optimizedTrail = cornerOffsets(trail([measure({ z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } })]))
assert.equal(offsetChain(optimizedTrail.FR), '−60 µm')
assert.equal(offsetChain(optimizedTrail.RR), '−80 µm')
assert.equal(offsetChain(optimizedTrail.RL), '−100 µm')
const mixed = screwPlan({ '27.9,25.95': 1.8, '227.15,25.95': 1.85, '227.15,225.5': 1.77, '27.9,225.5': 1.81 })
assert.equal(mixed.useOptimized, false)
assert.equal(mixed.canOptimize, false)
assert.equal(Math.round(mixed.shown[0] * 1000), 0)
const csvSessions = [
  {
    started: 1762800000,
    ended: null,
    measurements: [
      { time: 1762800000, kind: 'probe', corner: 'FL', twice: true, z: { FL: 1.8 }, z2: { FL: 1.8 } },
    ],
  },
]
const csv = screwHistoryCsv(csvSessions)
assert.match(screwTimeLocal(1762800000), /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/)
assert.match(
  csv,
  /^started,ended,entry_kind,time,time_local,kind,corner,twice,pass,FL_measured_mm,FR_measured_mm,RR_measured_mm,RL_measured_mm,FL_table_um/
)
assert.match(csv, /,session,/)
assert.match(csv, new RegExp(screwTimeLocal(1762800000).replace(/[.*+?^${}()|[\]\\]/g, '\\$&')))
assert.match(csv, /probe,FL,1,1,1\.8/)
assert.match(csv, /probe,FL,1,2,1\.8/)
const excel = screwHistoryCsv(csvSessions, 'excel')
assert.match(excel, /^started;ended;entry_kind;time;time_local;/)
assert.match(excel, /probe;FL;1;1;1,8/)
assert.match(excel, /probe;FL;1;2;1,8/)
assert.equal(excel.includes('1.8'), false)
const quickSample = {
  time: 1762801680,
  kind: 'quick',
  corner: null,
  twice: false,
  z: { FL: -0.6821, FR: -0.676, RR: -0.8704, RL: -0.751 },
}
const parsed = parseScrewHistory({
  sessions: [
    { kind: 'quick', points: 'screws', started: 1762801500, ended: 1762801680, measurements: [quickSample] },
    {
      started: 1762800000,
      ended: 1762801200,
      measurements: [
        { time: 1762800300, kind: 'start', corner: null, twice: false, z: { FL: -0.6818, FR: -0.6758, RR: -0.8698, RL: -0.7508 } },
      ],
    },
    { kind: 'session', started: 5, ended: 6, measurements: [{ time: 5, kind: 'probe', corner: 'FR', twice: false, z: { FR: 1.85 } }] },
  ],
})
assert.deepEqual(parsed.map(s => s.kind), ['quick', 'session', 'session'])
assert.equal(parsed[0].measurements[0].kind, 'quick')
assert.equal(parsed[1].kind, 'session', 'an entry without kind stays a session')
// Where the corners were probed: an entry says "screws"; one that says nothing (an older file, an older backend) or
// something else counts as the earlier measuring points and is never read as a measurement at the screw centres.
assert.deepEqual(parsed.map(s => s.points), ['screws', 'old', 'old'])
assert.equal(parseScrewHistory({ sessions: [{ kind: 'session', points: 'anything', started: 1, ended: 2, measurements: [] }] })[0].points, 'old')
const quickUm = measurementOffsets(parsed[0].measurements[0])
assert.equal(Math.round(quickUm.FR * 1000), 6)
assert.equal(Math.round(quickUm.RR * 1000), -188)
assert.equal(Math.round(quickUm.RL * 1000), -69)
const isolated = cornerOffsets({
  kind: 'session',
  started: 1,
  ended: null,
  measurements: [
    measure({ z: { FL: 1.8, FR: 1.85, RR: 1.82, RL: 1.79 } }),
    measure({ time: 2, kind: 'quick', z: quickSample.z }),
  ],
})
assert.equal(offsetChain(isolated.FR), '+50 µm', 'a quick sample does not join the session trail')
assert.match(screwHistoryCsv(parsed), /entry_kind/)
assert.match(screwHistoryCsv(parsed), /,quick,/)
assert.match(screwHistoryCsv(parsed), /,session,/)
const capped = parseScrewHistory({
  sessions: [
    ...Array.from({ length: 21 }, (_, i) => ({ started: 3000 - i, ended: 1, measurements: [] })),
    ...Array.from({ length: 11 }, (_, i) => ({
      kind: 'quick',
      started: 4000 - i,
      ended: 1,
      measurements: [{ time: 1, kind: 'quick', corner: null, twice: false, z: { FL: 1 } }],
    })),
  ],
})
assert.equal(capped.filter(s => s.kind === 'session').length, 20)
assert.equal(capped.filter(s => s.kind === 'quick').length, 10)

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
let browser
const actions = []
const errors = []
const shots = path.join(root, 'preview/artifacts')
const confirm = page => page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true })
const posted = (page, body) =>
  page.waitForRequest(r => r.method() === 'POST' && r.url().endsWith('/api/control') && r.postData() === body)
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
  await page.addInitScript(() => { try { localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' })); localStorage.setItem('cc2-screw-turn-mode', 'exact') } catch {} })
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() === 'POST' && r.url().endsWith('/api/control')) actions.push(r.postData())
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  // A profile the operator added on Control → Material Profiles, beside the factory ones.
  await page.route('**/api/material-presets', route =>
    route.fulfill({
      json: [
        { name: 'PLA', nozzle: 200, bed: 60, min: 190, max: 230 },
        { name: 'PETG', nozzle: 240, bed: 70, min: 220, max: 260 },
        { name: 'ABS', nozzle: 250, bed: 100, min: 230, max: 280 },
        { name: 'ASA-90', nozzle: 255, bed: 90, min: 240, max: 280 },
        { name: 'ABS-hot', nozzle: 260, bed: 130, min: 240, max: 280 },
        { name: 'PLA-62.5', nozzle: 205, bed: 62.5, min: 190, max: 230 },
        { name: 'Edge-110.0', nozzle: 260, bed: 110, min: 240, max: 280 },
        { name: 'Edge-110.4', nozzle: 260, bed: 110.4, min: 240, max: 280 },
        { name: 'Edge-110.5', nozzle: 260, bed: 110.5, min: 240, max: 280 },
        { name: 'Edge-110.6', nozzle: 260, bed: 110.6, min: 240, max: 280 },
        { name: 'Hot-120', nozzle: 260, bed: 120, min: 240, max: 280 },
      ],
    })
  )
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Bed alignment', exact: true }).click()
  const card = page.locator('#screwFocus')
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  // The hold time of step 1 goes in front of every start the user makes; 0 here (test-bed-mesh-tick covers it).
  await card.locator('[data-level-soak]').selectOption('0')
  const start = card.getByRole('button', { name: /Start session/ })
  const check = card.getByRole('button', { name: /Measure again/ })
  // "End session" is only in the session bar at the top (step 2 has none).
  const end = page.locator('[data-level-session-bar]').getByRole('button', { name: /End session/ })
  const measure = card.getByRole('button', { name: /Measure only \(quick\)/ })
  const fl = card.getByRole('button', { name: 'Single measurement: Front left', exact: true })
  const mode = name => card.locator(`[data-level-start-mode-button="${name}"]`).click()
  // Both starts are blocked: look at each mode in turn (only the button of the chosen mode is on the page).
  const bothBlocked = async () => {
    await mode('session')
    const session = await start.isDisabled()
    await mode('quick')
    return session && (await measure.isDisabled())
  }
  // Step 1: the mode "Quick" (default) offers the one-shot measurement, "With session" the session.
  await measure.waitFor()
  assert.ok(await measure.isEnabled(), 'one-shot measurement stays available')
  await card.getByText('No wait, heaters off afterwards').waitFor()
  await mode('session')
  await start.waitFor()
  assert.equal(await check.count(), 0, 'no check button before a session')
  assert.equal(await end.count(), 0, 'no session bar before a session')
  assert.ok(await fl.isDisabled(), 'corner re-probe is disabled before a session')
  await card.getByText('30 s wait before each measurement, off after 10 min idle').waitFor()
  assert.equal(await card.getByText('Each re-probe waits').count(), 0)
  await card.getByRole('checkbox', { name: '2× 3 probes', exact: true }).waitFor()
  assert.match(await card.locator('[data-hint="probe-twice"] button').getAttribute('aria-label'), /the value is the average/, 'the explanation sits in the (i) next to the choice')
  await card.locator('details[data-screw-help] summary').click() // the help is folded
  await card.locator('[data-screw-help-mode="auto"] strong').getByText('Auto', { exact: true }).waitFor()
  await card.getByText('Mesh capture stays separate.').waitFor()
  assert.equal(
    await card.locator('[data-screw-entries] > div').first().getByText('both heaters turn off afterwards').count(),
    0,
    'the session start does not claim both heaters turn off'
  )
  const history = card.locator('#screw-history')
  await history.getByText('Earlier sessions', { exact: true }).click()
  // The trail is a table: one row per measurement; the session of two measurements has a head row with its final values.
  const sessionTable = history.locator('[data-screw-history-table="sessions"]')
  await sessionTable.locator('[data-screw-session] [data-col="RR"]').getByText('−188', { exact: true }).waitFor()
  await sessionTable.locator('[data-screw-session] [data-col="FR"]').getByText('+6', { exact: true }).waitFor()
  await history.getByText('Quick measurements', { exact: true }).waitFor()
  const quickTable = history.locator('[data-screw-history-table="quick"]')
  const quick = quickTable.locator('[data-screw-kind="quick"]')
  assert.equal(await quick.count(), 1)
  await quick.locator('[data-col="kind"]').getByText('Quick measurement', { exact: true }).waitFor()
  assert.equal(await quick.locator('[data-hint="no-pause"] button').getAttribute('aria-label'), 'Measured without the 30 s pause.')
  await quick.locator('[data-col="FR"]').getByText('+6', { exact: true }).waitFor()
  await quick.locator('[data-col="RR"]').getByText('−188', { exact: true }).waitFor()
  assert.equal(await history.locator('[data-screw-kind="session"]').getByText('Quick measurement').count(), 0)
  // Entries that do not say they were probed at the screw centres carry a note: their values are not today's points.
  assert.match(
    await history.locator('[data-screw-old-points] button').first().getAttribute('aria-label'),
    /Measured at the earlier points/
  )
  assert.deepEqual(
    await page.evaluate(async () => (await (await fetch('/api/printer')).json()).screws_session),
    { active: false, xy_homed: false, bed: null }
  )
  await page.screenshot({ path: path.join(shots, 'screw-tram-idle-1440.png'), fullPage: true })
  await start.click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('Afterwards: nozzle off, the bed stays at').waitFor()
  await dialog.getByText('waits 30 s before probing').waitFor()
  assert.equal(await dialog.getByText('both heaters turn off').count(), 0)
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(actions.length, 0, 'cancel must not start a session')
  const started = posted(page, 'screws:tram:start:60:2')
  await start.click()
  await confirm(page).click()
  await started
  await page.getByText('Session active', { exact: true }).first().waitFor()
  assert.deepEqual(actions, ['screws:tram:start:60:2'])
  assert.deepEqual(
    await page.evaluate(async () => (await (await fetch('/api/printer')).json()).screws_session),
    { active: true, xy_homed: true, bed: 60 }
  )
  assert.ok(await check.isEnabled())
  assert.ok(await end.isEnabled())
  assert.ok(await fl.isEnabled())
  // The quick mode is locked during a session and its start button is not offered; a second start is locked too.
  assert.ok(await card.locator('[data-level-start-mode-button="quick"]').isDisabled(), 'the quick mode is locked during a session')
  assert.equal(await measure.count(), 0, 'no one-shot measurement during a session')
  assert.ok(await start.isDisabled(), 'a second session start is locked during a session')
  const header = page.locator('.cc2-header-status')
  await header.getByText('Levelling', { exact: true }).waitFor()
  let preview = null
  const fulfill = (body) => ({ contentType: 'application/json', body: JSON.stringify(body) })
  await page.route(/\/api\/console(\?.*)?$/, route => {
    if (!preview?.console || route.request().method() !== 'GET') return route.fallback()
    return route.fulfill(fulfill(preview.console))
  })
  await page.route('**/api/printer', async route => {
    if (!preview?.printer) return route.fallback()
    const data = await (await route.fetch()).json()
    Object.assign(data.machine, preview.printer)
    data.screws_session = { active: true, xy_homed: true }
    return route.fulfill({ json: data })
  })
  await page.route('**/api/screws/history', route => {
    if (!preview?.history || route.request().method() !== 'GET') return route.fallback()
    return route.fulfill(fulfill({ sessions: preview.history }))
  })
  preview = {
    console: { busy: true, completed: false, success: false, generation: 0, command: '', output: '' },
  }
  await header.getByText('Levelling', { exact: true }).waitFor({ timeout: 6000 })
  preview = {
    ...preview,
    printer: { status: 2, status_name: 'Printing' },
  }
  await header.getByText('Printing', { exact: true }).waitFor({ timeout: 6000 })
  assert.equal(await header.getByText('Levelling').count(), 0, 'other states stay unchanged')
  preview = { console: preview.console }
  await header.getByText('Levelling', { exact: true }).waitFor({ timeout: 6000 })
  preview = null
  await header.getByText('Levelling', { exact: true }).waitFor({ timeout: 6000 })
  await page.waitForFunction(() => {
    const button = [...document.querySelectorAll('button')].find(e => e.getAttribute('aria-label') === 'Single measurement: Front left')
    return button && !button.disabled
  })
  // The picture keeps the signed difference; the table says how far to move (direction by arrow and words).
  await card.getByText('+50 µm').first().waitFor()
  await card.locator('[data-screw-exact="FR"]').getByText('25.7°', { exact: true }).waitFor()
  await card.locator('[data-screw-mode] select').selectOption('auto')
  await card.locator('[data-screw-aid="FR"]').getByText('25° · remaining −1 µm', { exact: true }).waitFor()
  await card.locator('[data-screw-mode] select').selectOption('exact')
  assert.equal(await card.getByText(/vs last/).count(), 0, 'the saved trail replaces the previous delta')
  await page.screenshot({ path: path.join(shots, 'screw-tram-session-1440.png'), fullPage: true })
  const probed = posted(page, 'screws:tram:probe:FL:2')
  await fl.click()
  await probed
  // Few words in the table: "Deviation" with a small (i) stands only where the single probes of a corner in this measurement spread by
  // more than 10 µm; no comparison with the earlier measurement, and without written-down probes nothing (the preview sends none here).
  await card.getByText('no longer match').waitFor()
  assert.equal(await card.getByText(/1st→2nd/).count(), 0, 'no text line for the difference between the two probes')
  assert.equal(await card.locator('[data-screw-deviation]').count(), 0, 'no single probes known: no deviation')
  await card.locator('[data-screw-unit] select').selectOption('mm')
  assert.equal(await card.locator('tbody tr td:nth-child(2)').filter({ hasText: 'µm' }).count(), 0, 'no µm in the Measured column while the unit is mm')
  await card.locator('[data-screw-unit] select').selectOption('um')
  assert.equal(actions.at(-1), 'screws:tram:probe:FL:2', 'default probe-twice sends :2')
  const checked = posted(page, 'screws:tram:check:2')
  await check.click()
  await confirm(page).click()
  await checked
  assert.equal(actions.at(-1), 'screws:tram:check:2')
  // The full check changed the measured height of front right (the difference to front left is no longer written in Measured).
  await card.locator('[data-screw-zheight="FR"]').waitFor()
  assert.equal(await card.getByText(/vs front left/).count(), 0)
  await card.getByText('no longer match').waitFor({ state: 'detached', timeout: 8000 }) // a full check drops the stale notice
  assert.equal(await card.getByText('still apply').count(), 0)
  await page.setViewportSize({ width: 390, height: 844 })
  await card.scrollIntoViewIfNeeded()
  assert.ok(
    await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1),
    'mobile must not overflow'
  )
  await page.screenshot({ path: path.join(shots, 'screw-tram-session-390.png'), fullPage: true })
  await page.setViewportSize({ width: 1440, height: 1000 })
  const ended = posted(page, 'screws:tram:end')
  await end.click()
  await confirm(page).click()
  await ended
  await page.waitForFunction(() => ![...document.querySelectorAll('span, small')].some(e => e.textContent === 'Session active'))
  assert.equal(actions.at(-1), 'screws:tram:end')
  assert.ok(await fl.isDisabled())
  await mode('quick')
  assert.ok(await measure.isEnabled())
  // Step 1 (basis) gives the bed target: a material profile writes its bed temperature (whole degrees, halves
  // up) into step 2; a value outside 30..110 C is not measured (both starts are blocked with a message).
  const basisSelect = card.locator('[data-level-base]')
  const options = await basisSelect.locator('option').allTextContents()
  for (const label of ['PLA · 60 °C', 'PETG · 70 °C', 'ASA-90 · 90 °C', 'PLA-62.5 · 63 °C', 'Edge-110.0 · 110 °C', 'Hot-120 · 120 °C'])
    assert.ok(options.includes(label), `the basis lists ${label}`)
  const pick = name => basisSelect.selectOption(`material:${name}`)
  const invalid = card.locator('[data-level-temp-invalid]')
  // Step 2 has no temperature field of its own: the temperature comes from step 1 only.
  assert.equal(await card.locator('[data-level-step="2"] input').count(), 0, 'step 2 has no input')
  // Any other temperature goes through "Own temperature" in step 1; a wrong one is reported there and blocks both starts.
  await basisSelect.selectOption('custom')
  const own = card.locator('[data-level-custom-temp]')
  await own.fill('100')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  assert.equal(await invalid.count(), 0)
  const hundred = posted(page, 'screws:measure:100:2')
  await mode('quick')
  await measure.click()
  await dialog.getByText('Heat the bed to 100 °C').waitFor()
  await confirm(page).click()
  await hundred
  await own.fill('111')
  await invalid.waitFor()
  assert.equal(await card.locator('[data-level-step="1"] [data-level-temp-invalid]').count(), 1, 'the message sits in step 1')
  assert.ok(await bothBlocked(), '111 C blocks both starts')
  await pick('PLA')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  await pick('PETG')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  await mode('session')
  await start.click()
  await dialog.getByText('heat the bed to 70 °C').waitFor()
  await dialog.getByText('the bed stays at 70 °C').waitFor()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(await invalid.count(), 0, 'a profile within the limit shows no warning')
  // A profile above the bed's 110 °C: nothing is measured, both starts stay blocked.
  await pick('ABS-hot')
  await invalid.first().waitFor()
  assert.ok(await bothBlocked(), 'above 110 C both starts are blocked')
  await pick('ASA-90')
  assert.equal(await invalid.count(), 0, 'another profile clears the warning')
  const measured = posted(page, 'screws:measure:90:2')
  await mode('quick')
  await measure.click()
  await dialog.getByText('Heat the bed to 90 °C').waitFor()
  await confirm(page).click()
  await measured
  assert.equal(actions.at(-1), 'screws:measure:90:2', 'the one-shot measurement sends the 90 C of the added profile, 2× 3 probes')
  // Decimals are rounded half up to whole degrees.
  await pick('PLA-62.5')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  const rounded = posted(page, 'screws:measure:63:2')
  await mode('quick')
  await measure.click()
  await dialog.getByText('Heat the bed to 63 °C').waitFor()
  await confirm(page).click()
  await rounded
  assert.equal(actions.at(-1), 'screws:measure:63:2', '62.5 C is measured at 63 C')
  // The limit of 110 C applies to the whole degrees: 110.0 and 110.4 are 110, 110.5 and more are blocked.
  await pick('Edge-110.0')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  assert.ok((await invalid.count()) === 0 && (await (async () => { await mode('session'); return start.isEnabled() })()), '110.0 C is within the limit')
  await pick('Edge-110.4')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  assert.ok((await invalid.count()) === 0 && (await (async () => { await mode('session'); return start.isEnabled() })()), '110.4 C is 110 C')
  for (const name of ['Edge-110.5', 'Edge-110.6', 'Hot-120']) {
    await pick(name)
    await invalid.first().waitFor()
    assert.ok(await bothBlocked(), `${name} is above 110 C: both starts are blocked`)
  }
  await pick('PLA')
  // (Step 1 shows the temperature in the basis choice itself; the dialog of the action names it below.)
  const jsonDownload = page.waitForEvent('download')
  await history.getByRole('button', { name: 'Export JSON', exact: true }).click()
  const jsonFile = await jsonDownload
  assert.equal(jsonFile.suggestedFilename(), 'screw-levelling-history.json')
  const jsonText = await readFile(await jsonFile.path(), 'utf8')
  assert.match(jsonText, /"kind": "quick"/)
  assert.match(jsonText, /"kind": "session"/)
  const csvDownload = page.waitForEvent('download')
  await history.getByRole('button', { name: 'Export CSV', exact: true }).click()
  const csvFile = await csvDownload
  assert.equal(csvFile.suggestedFilename(), 'screw-levelling-history.csv')
  const csvText = await readFile(await csvFile.path(), 'utf8')
  assert.match(
    csvText,
    /started,ended,entry_kind,time,time_local,kind,corner,twice,pass,FL_measured_mm,.*FL_table_um/
  )
  assert.match(csvText, /probe,FL,1,1,/)
  assert.match(csvText, /probe,FL,1,2,/)
  assert.match(csvText, /,session,/)
  assert.match(csvText, /,quick,/)
  assert.match(csvText, /\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}/)
  const excelDownload = page.waitForEvent('download')
  await history.getByRole('button', { name: 'CSV for Excel (DE)', exact: true }).click()
  const excelFile = await excelDownload
  assert.equal(excelFile.suggestedFilename(), 'screw-levelling-history-excel.csv')
  const excelText = await readFile(await excelFile.path(), 'utf8')
  assert.match(excelText, /started;ended;entry_kind;time;time_local;/)
  assert.match(excelText, /;quick;/)
  assert.match(excelText, /;session;/)
  assert.match(excelText, /1,78/)
  assert.equal(excelText.includes('FL_um'), false)
  const tramOutput = (heights) =>
    `SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n${Object.entries(heights)
      .map(([key, z]) => {
        const [x, y] = key.split(',')
        const height = Number(z).toFixed(6)
        return `${`// probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${height}\n`.repeat(3)}// Result is z=${height}\n`
      })
      .join('')}`
  const ROW_IDS = { 'Front left': 'FL', 'Front right': 'FR', 'Back right': 'RR', 'Back left': 'RL', 'Vorne links': 'FL', 'Vorne rechts': 'FR', 'Hinten rechts': 'RR', 'Hinten links': 'RL' }
  // The row of a corner (its first cell also holds the number button 1–4).
  const row = name => card.locator(`tbody tr[data-screw-row="${ROW_IDS[name]}"]`)
  let generation = 40
  const show = async (heights, measurements, secondPass = '') => {
    generation += 1
    preview = {
      console: {
        busy: false,
        completed: true,
        success: true,
        generation,
        command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n',
        output: tramOutput(heights) + secondPass,
      },
      history: [{ started: 10, ended: null, measurements }],
    }
  }
  await show(
    { '27.9,25.95': 1.8, '227.15,25.95': 1.74, '227.15,225.5': 1.72, '27.9,225.5': 1.7 },
    [{ time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } }]
  )
  const frontLeft = row('Front left')
  const frontRight = row('Front right')
  // Standard stays: front left is the fixed reference and has nothing to adjust. The optimized plan (front left is the highest
  // corner: lower it) is only offered, in its own note with a button, and nothing switches to it by itself.
  const offer = card.locator('[data-screw-optimize-suggest]')
  await offer.waitFor()
  await offer.getByText(/Also lower the front-left screw by 60/).waitFor()
  assert.equal((await frontLeft.locator('[data-screw-adjust="FL"]').innerText()).trim(), 'No turn', 'standard: front left is left as it is')
  assert.equal((await frontLeft.locator('[data-screw-goal="FL"]').innerText()).trim(), 'Leave unchanged')
  assert.equal(await card.locator('[data-screw-combined-note]').count(), 0, 'no combined note before the choice')
  await offer.getByRole('button', { name: 'Choose optimized correction', exact: true }).click()
  await frontLeft.locator('[data-screw-amount="FL"]').getByText('60 µm', { exact: true }).waitFor()
  await frontLeft.locator('[data-screw-direction="lower"]').getByText('lower', { exact: false }).waitFor()
  await card.getByText(/Also lower the front-left screw by 60/).waitFor()
  assert.equal(await offer.count(), 0, 'the offer is gone once the plan is chosen')
  assert.equal(await frontLeft.getByText('Leave the screw', { exact: true }).count(), 0)
  await card.getByText('recommended turns').waitFor()
  // Measured shows the height as measured (never the difference: the Adjustment column holds that).
  await card.locator('[data-screw-zheight="FR"]').getByText('1740 µm').waitFor()
  await card.locator('[data-screw-zheight="RR"]').getByText('1720 µm').waitFor()
  await card.locator('[data-screw-zheight="RL"]').getByText('1700 µm').waitFor()
  assert.equal(await card.getByText(/vs front left/).count(), 0, 'Measured shows no difference to front left')
  assert.match(await frontRight.innerText(), /Within tolerance/, 'front right is within the tolerance of the combined plan')
  await show(
    { '27.9,25.95': 1.8, '227.15,25.95': 1.85, '227.15,225.5': 1.77, '27.9,225.5': 1.81 },
    [{ time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.85, RR: 1.77, RL: 1.81 } }]
  )
  await card.locator('[data-screw-zheight="FR"]').getByText('1850 µm').waitFor()
  assert.equal((await frontLeft.locator('[data-screw-adjust="FL"]').innerText()).trim(), 'No turn', 'front left as the reference: no turn')
  assert.equal((await frontLeft.locator('[data-screw-goal="FL"]').innerText()).trim(), 'Leave unchanged', 'front left as the reference: left unchanged')
  await card.locator('[data-screw-zheight="RR"]').getByText('1770 µm').waitFor()
  await card.locator('[data-screw-zheight="RL"]').getByText('1810 µm').waitFor()
  await show(
    { '27.9,25.95': 1.802, '227.15,25.95': 1.85, '227.15,225.5': 1.82, '27.9,225.5': 1.79 },
    [
      { time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.85, RR: 1.82, RL: 1.79 } },
      { time: 11, kind: 'probe', corner: 'FL', twice: true, z: { FL: 1.802 }, z2: { FL: 1.804 } },
    ]
  )
  await card.getByText('Reference is stable (+3 µm)').waitFor()
  // A move of 3 µm (the average of 1.802 and 1.804) is within the scatter: nothing but the Z height in the cell.
  assert.equal(await card.locator('[data-screw-deviation]').count(), 0, 'no deviation for 3 µm')
  await card.locator('[data-screw-unit] select').selectOption('mm')
  await card.getByText('Reference is stable (+0.003 mm)').waitFor()
  await card.locator('[data-screw-unit] select').selectOption('um')
  assert.equal(await card.getByText('0 → +10 µm').count(), 0, '2× 3 keeps the first sample')
  await show(
    { '27.9,25.95': 1.812, '227.15,25.95': 1.74, '227.15,225.5': 1.72, '27.9,225.5': 1.7 },
    [
      { time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } },
      { time: 11, kind: 'probe', corner: 'FL', twice: false, z: { FL: 1.812 } },
    ]
  )
  // The optimized plan is chosen and front left moved: no new plan before all four corners are measured again.
  await card.locator('[data-screw-four-needed]').getByText('Four-corner measurement needed', { exact: true }).waitFor()
  assert.equal(await card.locator('[data-screw-proposal-block]').count(), 0, 'no adjustment plan before the four-corner measurement')
  assert.equal(await card.locator('[data-screw-deviation]').count(), 0, 'a moved reference is no deviation of a corner')
  const startAll = { time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } }
  const heights = { '27.9,25.95': 1.8, '227.15,25.95': 1.74, '227.15,225.5': 1.72, '27.9,225.5': 1.7 }
  const hint = async corner => {
    await card.locator(`[data-screw-deviation="${corner}"] button`).hover()
    const text = (await card.locator('[data-hint-text]').innerText()).replace(/\u00a0/g, ' ')
    await page.mouse.move(5, 5)
    return text
  }
  // The single probes of FR spread by 12 µm (max minus min): "Deviation", the probes and "measure again" in the hint, in the unit.
  await show(heights, [{ ...startAll, probes: { FR: [1.734, 1.74, 1.746] } }])
  await card.locator('[data-screw-deviation="FR"]').getByText('Deviation', { exact: true }).waitFor()
  assert.equal(await card.locator('[data-screw-deviation]').count(), 1, 'only the corner with the large spread')
  assert.equal(await hint('FR'), 'Single probes: 1734 µm · 1740 µm · 1746 µm. Spread 12 µm. Measure again.')
  await card.locator('[data-screw-unit] select').selectOption('mm')
  assert.equal(await hint('FR'), 'Single probes: 1.734 mm · 1.740 mm · 1.746 mm. Spread 0.012 mm. Measure again.')
  await card.locator('[data-screw-unit] select').selectOption('um')
  // 8 µm of spread is within the scatter; and a corner without written-down probes shows nothing.
  await show(heights, [{ ...startAll, probes: { FR: [1.736, 1.74, 1.744], RR: [1.7, 1.72, 1.74] } }])
  await page.waitForFunction(() => document.querySelector('[data-screw-deviation="FR"]') === null)
  await card.locator('[data-screw-deviation="RR"]').waitFor() // 40 µm of spread at RR
  await show(heights, [startAll])
  await page.waitForFunction(() => document.querySelector('[data-screw-deviation]') === null)
  // Two results of a corner (2× 3): one value, the average of both (1746 µm).
  await show(heights, [startAll], `${'// probe at 227.150,25.950 is z=1.752\n'.repeat(3)}// Result is z=1.752\n`)
  await card.locator('[data-screw-zheight="FR"]').getByText('1746 µm').waitFor()
  // "±x" behind the difference when it lies within x of the 20 µm limit of the turn instruction: x = 5 µm once, 3 µm with 2× 3.
  const tolerance = corner => card.locator(`[data-screw-tolerance="${corner}"]`)
  const near = (fr, twice) => show({ ...heights, '227.15,25.95': fr }, [{ ...startAll, twice, z: { ...startAll.z, FR: fr }, ...(twice ? { z2: { FR: fr } } : {}) }])
  await near(1.82, false)
  await tolerance('FR').waitFor()
  assert.equal((await tolerance('FR').innerText()).replace(/\u00a0/g, ' '), '±5 µm')
  await card.locator('[data-screw-unit] select').selectOption('mm')
  assert.equal((await tolerance('FR').innerText()).replace(/\u00a0/g, ' '), '±0.005 mm', 'in the chosen unit')
  await card.locator('[data-screw-unit] select').selectOption('um')
  await near(1.82, true)
  await page.waitForFunction(() => document.querySelector('[data-screw-tolerance="FR"]')?.textContent?.includes('3'))
  assert.equal((await tolerance('FR').innerText()).replace(/\u00a0/g, ' '), '±3 µm', '2× 3: the average of six, ±3 µm')
  await near(1.8239, true) // 24 µm: 4 µm from the limit, more than 3
  await page.waitForFunction(() => document.querySelector('[data-screw-tolerance="FR"]') === null)
  await near(1.8239, false) // the same 24 µm once: 4 µm from the limit, within 5
  await tolerance('FR').waitFor()
  await near(1.83, false) // 30 µm: 10 µm from the limit
  await page.waitForFunction(() => document.querySelector('[data-screw-tolerance="FR"]') === null)
  assert.equal(await tolerance('RR').count(), 0, 'a corner far from the limit has no ±')
  assert.equal(await card.getByText('no longer match').count(), 0)
  preview = null
  await history.locator('[data-screw-history-table] [data-col="RR"]').getByText('−188', { exact: true }).first().waitFor()
  const cleared = page.waitForRequest(
    r => r.method() === 'POST' && r.url().endsWith('/api/screws/history/clear')
  )
  await history.getByRole('button', { name: 'Clear history', exact: true }).click()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(
    await page.evaluate(async () => (await (await fetch('/api/screws/history')).json()).sessions.length),
    3,
    'cancel keeps the history'
  )
  await history.getByRole('button', { name: 'Clear history', exact: true }).click()
  await confirm(page).click()
  const clearRequest = await cleared
  assert.equal(clearRequest.headers()['x-cc2-request'], '1')
  await history.getByText('No earlier sessions.', { exact: true }).waitFor()
  await page.route('**/api/screws/history', route => {
    const url = new URL(route.request().url())
    if (route.request().method() === 'GET' && url.pathname === '/api/screws/history')
      return route.fulfill({ status: 404, contentType: 'application/json', body: '{"error":"not found"}' })
    return route.fallback()
  })
  await page.reload()
  await page.locator('#cc2-preview-banner').waitFor()
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Bed alignment', exact: true }).click()
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  await mode('session')
  await start.waitFor()
  assert.equal(await card.locator('#screw-history').count(), 0, 'a missing history endpoint hides the trail')
  assert.equal(await card.getByText('30 s wait before each measurement, off after 10 min idle').count(), 1)
  assert.deepEqual(errors, [])
  console.log('PASS screw-tram session, copy, history trail, export, clear, 404, and layout')
} finally {
  await browser?.close()
  await server.close()
}
