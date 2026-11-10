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
assert.equal(twice.change, '0 → +2 µm', '2× 3 uses the first sample')
assert.equal(
  referenceMove(trail([measure({ z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } })])),
  null,
  'a single front-left reading shows no reference shift'
)
assert.equal(twice.sinceFullUm, 2)
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
const optimized = screwPlan({ '30,30': 1.8, '230,30': 1.74, '230,225': 1.72, '30,225': 1.7 })
assert.equal(optimized.useOptimized, true)
assert.deepEqual(
  optimized.shown.map(v => Math.round(v * 1000)),
  [-60, 0, -20, -40]
)
const optimizedTrail = cornerOffsets(trail([measure({ z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } })]))
assert.equal(offsetChain(optimizedTrail.FR), '−60 µm')
assert.equal(offsetChain(optimizedTrail.RR), '−80 µm')
assert.equal(offsetChain(optimizedTrail.RL), '−100 µm')
const mixed = screwPlan({ '30,30': 1.8, '230,30': 1.85, '230,225': 1.77, '30,225': 1.81 })
assert.equal(mixed.useOptimized, false)
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
    { kind: 'quick', started: 1762801500, ended: 1762801680, measurements: [quickSample] },
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
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() === 'POST' && r.url().endsWith('/api/control')) actions.push(r.postData())
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Screw Levelling', exact: true }).click()
  const card = page.locator('#screwFocus')
  await card.waitFor()
  const start = card.getByRole('button', { name: /Start session/ })
  const check = card.getByRole('button', { name: /Check all four/ })
  const end = card.getByRole('button', { name: /End session/ })
  const measure = card.getByRole('button', { name: 'Measure the four screws', exact: true })
  const fl = card.getByRole('button', { name: 'Re-probe Front left', exact: true })
  await start.waitFor()
  assert.ok(await check.isDisabled(), 'check is disabled before a session')
  assert.ok(await end.isDisabled(), 'end is disabled before a session')
  assert.ok(await fl.isDisabled(), 'corner re-probe is disabled before a session')
  assert.ok(await measure.isEnabled(), 'one-shot measurement stays available')
  await card.getByText('only the nozzle turns off').waitFor()
  await card.getByText('Every measurement waits 30 s').first().waitFor()
  assert.equal(await card.getByText('Every measurement waits 30 s').count(), 1, 'the settle is stated once on the page')
  await card.getByText('level again and check the mesh').waitFor()
  assert.equal(await card.getByText('Each re-probe waits').count(), 0)
  await card.getByRole('checkbox', { name: '2× 3 probes', exact: true }).waitFor()
  await card.getByText('has not settled yet').waitFor()
  await card.getByText('6 per corner').waitFor()
  await card.getByText('Mesh capture stays separate.').waitFor()
  assert.equal(await card.getByText('both heaters turn off afterwards').count(), 0)
  const history = card.locator('#screw-history')
  await history.getByText('Earlier sessions', { exact: true }).click()
  await history.getByText('−188 µm').first().waitFor()
  await history.getByText('Front right +6 µm').first().waitFor()
  await history.getByText('Quick measurements', { exact: true }).waitFor()
  const quick = history.locator('[data-screw-kind="quick"]')
  await quick.getByText('Quick measurement · no 30 s pause', { exact: true }).waitFor()
  await quick.getByText('Front right +6 µm').waitFor()
  await quick.getByText('Back right −188 µm').waitFor()
  assert.equal(await history.locator('[data-screw-kind="session"]').getByText('Quick measurement').count(), 0)
  assert.deepEqual(
    await page.evaluate(async () => (await (await fetch('/api/printer')).json()).screws_session),
    { active: false, xy_homed: false }
  )
  await page.screenshot({ path: path.join(shots, 'screw-tram-idle-1440.png'), fullPage: true })
  await start.click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('only the nozzle turns off').waitFor()
  await dialog.getByText('waits 30 s before probing').waitFor()
  assert.equal(await dialog.getByText('both heaters turn off').count(), 0)
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(actions.length, 0, 'cancel must not start a session')
  const started = posted(page, 'screws:tram:start')
  await start.click()
  await confirm(page).click()
  await started
  await page.getByText('Session active', { exact: true }).waitFor()
  assert.deepEqual(actions, ['screws:tram:start'])
  assert.deepEqual(
    await page.evaluate(async () => (await (await fetch('/api/printer')).json()).screws_session),
    { active: true, xy_homed: true }
  )
  assert.ok(await check.isEnabled())
  assert.ok(await end.isEnabled())
  assert.ok(await fl.isEnabled())
  assert.ok(await measure.isDisabled(), 'one-shot measurement is disabled during a session')
  const header = page.locator('.cc2-header-status')
  await header.getByText('Idle', { exact: true }).waitFor()
  let preview = null
  const fulfill = (body) => ({ contentType: 'application/json', body: JSON.stringify(body) })
  await page.route('**/api/console', route => {
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
  await header.getByText('Screw measurement running', { exact: true }).waitFor({ timeout: 6000 })
  preview = {
    ...preview,
    printer: { status: 2, status_name: 'Printing' },
  }
  await header.getByText('Printing', { exact: true }).waitFor({ timeout: 6000 })
  assert.equal(await header.getByText('Screw measurement running').count(), 0, 'other states stay unchanged')
  preview = { console: preview.console }
  await header.getByText('Screw measurement running', { exact: true }).waitFor({ timeout: 6000 })
  preview = null
  await header.getByText('Idle', { exact: true }).waitFor({ timeout: 6000 })
  await page.waitForFunction(() => {
    const button = [...document.querySelectorAll('button')].find(e => e.getAttribute('aria-label') === 'Re-probe Front left')
    return button && !button.disabled
  })
  await card.getByText('+50 µm').first().waitFor()
  await card.getByText('+50 µm').nth(1).waitFor()
  assert.equal(await card.getByText(/vs last/).count(), 0, 'the saved trail replaces the previous delta')
  await page.screenshot({ path: path.join(shots, 'screw-tram-session-1440.png'), fullPage: true })
  const probed = posted(page, 'screws:tram:probe:FL:2')
  await fl.click()
  await probed
  await card.getByText('1st→2nd: −1 µm').waitFor()
  await card.getByText('Reference moved: 0 → −20 µm').waitFor()
  await card.getByText('no longer match').waitFor()
  assert.equal(actions.at(-1), 'screws:tram:probe:FL:2', 'default probe-twice sends :2')
  const checked = posted(page, 'screws:tram:check:2')
  await check.click()
  await confirm(page).click()
  await checked
  assert.equal(actions.at(-1), 'screws:tram:check:2')
  await card.getByText('vs front left: +50 → +71 µm').waitFor()
  assert.equal(await card.getByText('no longer match').count(), 0, 'a full check drops the stale notice')
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
  await page.getByText('Session active', { exact: true }).waitFor({ state: 'hidden' })
  assert.equal(actions.at(-1), 'screws:tram:end')
  assert.ok(await fl.isDisabled())
  assert.ok(await measure.isEnabled())
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
        return `${`// probe at ${x}.000,${y}.000 is z=${height}\n`.repeat(3)}// Result is z=${height}\n`
      })
      .join('')}`
  const row = (name) => card.locator('tbody tr').filter({ has: page.getByRole('cell', { name, exact: true }) })
  let generation = 40
  const show = async (heights, measurements) => {
    generation += 1
    preview = {
      console: {
        busy: false,
        completed: true,
        success: true,
        generation,
        command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n',
        output: tramOutput(heights),
      },
      history: [{ started: 10, ended: null, measurements }],
    }
  }
  await show(
    { '30,30': 1.8, '230,30': 1.74, '230,225': 1.72, '30,225': 1.7 },
    [{ time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } }]
  )
  const frontLeft = row('Front left')
  const frontRight = row('Front right')
  await frontLeft.getByText('−60 µm').waitFor()
  assert.equal(/\bZero\b/.test(await frontLeft.innerText()), false)
  assert.equal(await frontLeft.getByText('Leave it', { exact: true }).count(), 0)
  await card.getByText('recommended turns').waitFor()
  await frontRight.getByText('vs front left: −60 µm').waitFor()
  await row('Back right').getByText('vs front left: −80 µm').waitFor()
  await row('Back left').getByText('vs front left: −100 µm').waitFor()
  assert.match(await frontRight.innerText(), /0 µm/)
  await show(
    { '30,30': 1.8, '230,30': 1.85, '230,225': 1.77, '30,225': 1.81 },
    [{ time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.85, RR: 1.77, RL: 1.81 } }]
  )
  await frontLeft.getByText('Zero').waitFor()
  await frontLeft.getByText('Leave it', { exact: true }).waitFor()
  await frontRight.getByText('vs front left: +50 µm').waitFor()
  await row('Back right').getByText('vs front left: −30 µm').waitFor()
  await row('Back left').getByText('vs front left: +10 µm').waitFor()
  await show(
    { '30,30': 1.802, '230,30': 1.85, '230,225': 1.82, '30,225': 1.79 },
    [
      { time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.85, RR: 1.82, RL: 1.79 } },
      { time: 11, kind: 'probe', corner: 'FL', twice: true, z: { FL: 1.802 }, z2: { FL: 1.81 } },
    ]
  )
  await card.getByText('Reference is stable (+2 µm)').waitFor()
  await card.getByText('Reference moved: 0 → +2 µm').waitFor()
  assert.equal(await card.getByText('0 → +10 µm').count(), 0, '2× 3 keeps the first sample')
  await show(
    { '30,30': 1.812, '230,30': 1.74, '230,225': 1.72, '30,225': 1.7 },
    [
      { time: 10, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 } },
      { time: 11, kind: 'probe', corner: 'FL', twice: false, z: { FL: 1.812 } },
    ]
  )
  await card.getByText('next step of the optimised plan').waitFor()
  await card.getByText('Reference moved: 0 → +12 µm').waitFor()
  assert.equal(await card.getByText('no longer match').count(), 0)
  preview = null
  await history.getByText('−188 µm').first().waitFor()
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
  await page.getByRole('tab', { name: 'Screw Levelling', exact: true }).click()
  await card.waitFor()
  await start.waitFor()
  assert.equal(await card.locator('#screw-history').count(), 0, 'a missing history endpoint hides the trail')
  assert.equal(await card.getByText('only the nozzle turns off').count(), 1)
  assert.deepEqual(errors, [])
  console.log('PASS screw-tram session, copy, history trail, export, clear, 404, and layout')
} finally {
  await browser?.close()
  await server.close()
}
