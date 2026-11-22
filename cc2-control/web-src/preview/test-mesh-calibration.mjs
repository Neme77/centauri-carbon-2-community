import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Bed Levelling > Mesh calibration runs on CC2 Control: the page sends the side, bed temperature, soak time,
// nozzle and plate once, then follows the stages (homing, heating, soaking, probing, saving), can stop the run
// before the probing and reports the outcome. The preview moves the run one stage on per step request.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], plateCalls = [], consoleCalls = []
  let meshLoads = 0
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    const url = new URL(r.url()).pathname
    if (url === '/api/mesh') meshLoads++
    if (url.startsWith('/api/console/')) consoleCalls.push(url)
    if (r.method() === 'POST' && url.startsWith('/api/plates/')) {
      assert.equal(r.headers()['x-cc2-request'], '1', 'plate changes carry the mutation marker')
      plateCalls.push([url, r.postData()])
    }
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  let homed = ''
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.motion = { ...data.motion, homed_axes: homed }
    await route.fulfill({ response, json: data })
  })
  const step = (body = '') => page.evaluate(b => fetch('/__preview/calibration-step', { method: 'POST', headers: { 'X-CC2-Request': '1' }, body: b }), body)
  const progress = page.locator('[data-calibration-bar]')
  const toast = text => page.getByText(text, { exact: true }).waitFor()

  await page.goto(`${origin}/#bed`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')]) // the scenario reloads the page
  // Labels wrap their controls, so address the control inside each label.
  const field = text => page.locator('label', { hasText: text }).locator('select, input')
  const side = field('Calibration plate side'), temperature = field('Bed temperature, °C')
  const soak = field('Soak, min'), target = field('Save the result to plate')
  const run = page.getByRole('button', { name: /Run Bed Mesh Calibration/ })
  const dialog = page.getByRole('dialog')
  await side.waitFor()
  await side.selectOption('default')
  assert.equal(await target.inputValue(), 'a1b2c3d4e5f60718', 'the mounted Side A plate takes the result')
  assert.equal(await soak.inputValue(), '2', 'new calibrations default to a two-minute hold')
  assert.equal(await soak.locator('option').count(), 61, 'every whole minute from zero to sixty is selectable')
  assert.equal(await soak.locator('option[value="61"]').count(), 0, 'out-of-range times cannot be selected')

  // Invalid values never reach the printer.
  await temperature.fill('120')
  await soak.selectOption('10')
  await run.click()
  await toast('Use a whole bed temperature from 30 to 110 °C.')
  assert.equal(await dialog.count(), 0)
  assert.equal(plateCalls.length, 0)

  // Unhomed, 80 °C, a ten-minute soak and the mounted plate: one request starts the run on the printer.
  await temperature.fill('80')
  await run.click()
  for (const line of [
    'Start a new bed mesh calibration at 80 °C?',
    'Nozzle: 0.4 brass',
    'The result becomes the 80 °C measurement of “Smooth PEI”.',
    'The bed heats to 80 °C and is held there for 10 min before probing.',
    'The printer is not homed, so it homes X, Y and Z first.',
  ])
    await dialog.getByText(line, { exact: false }).waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await progress.waitFor()
  assert.deepEqual(plateCalls, [['/api/plates/calibrate', 'A\n80\n10\ndd00000000000004\na1b2c3d4e5f60718']])
  assert.equal(consoleCalls.length, 0, 'the page sends no console commands itself')
  await page.getByText('Homing X, Y and Z…', { exact: true }).waitFor()
  assert.ok(await run.isDisabled() && (await temperature.isDisabled()), 'nothing else starts while it runs')
  const stages = [
    ['heating', 'Heating the bed: 25 °C of 80 °C'],
    ['soaking', 'Holding the bed at 80 °C: 10:00 left'],
    ['probing', 'Probing Side A at 80 °C…'],
    ['saving', 'Saving the measurement to “Smooth PEI”…'],
  ]
  for (const [stage, text] of stages) {
    await step()
    await page.locator(`[data-calibration-bar="${stage}"]`).waitFor()
    await page.getByText(text, { exact: true }).waitFor()
    const stop = progress.getByRole('button', { name: 'Stop', exact: true })
    assert.equal(await stop.count(), ['probing', 'saving'].includes(stage) ? 0 : 1, `Stop while ${stage}`)
  }
  const loads = meshLoads
  await step()
  await toast('Saved to “Smooth PEI” as its 80 °C measurement.')
  await progress.waitFor({ state: 'detached' })
  for (let i = 0; i < 50 && meshLoads === loads; i++) await page.waitForTimeout(100)
  assert.ok(meshLoads > loads, 'the new mesh is read once the run ends')
  assert.ok(!(await run.isDisabled()))
  // The 80 °C measurement already existed, so the page opens the change from that mesh.
  const profile = page.locator('label', { hasText: 'Mesh profile' }).locator('select')
  await profile.waitFor()
  assert.equal(await profile.inputValue(), 'measure:aa00000000000080')
  const change = page.getByRole('button', { name: 'Mesh change', exact: true })
  assert.equal(await change.getAttribute('aria-pressed'), 'true')
  await page.locator('[data-mesh-diff="on"]').waitFor()
  await page.getByText('Blue is lower than the previous mesh', { exact: false }).waitFor()
  await page.getByRole('button', { name: 'Values', exact: true }).click()
  const row = page.locator('[data-mesh-diff="on"] tbody tr').first()
  assert.equal(await row.locator('td').first().innerText(), '-0.100', 'the low X edge fell')
  assert.equal(await row.locator('td').last().innerText(), '0.100', 'the high X edge rose')
  await page.locator('.cc2-mesh-stat', { hasText: 'Range' }).getByText('0.200').waitFor()
  await change.click()
  await page.locator('[data-mesh-diff="off"]').waitFor()
  await change.click()
  await page.locator('[data-mesh-diff="on"]').waitFor()
  await page.getByRole('button', { name: 'View 3D', exact: true }).click()

  // Toggling the comparison must not move the calibration button or the plot. The date caption and the
  // pressed button used to wrap the row at these widths.
  const box = loc => loc.evaluate(el => {
    const r = el.getBoundingClientRect()
    const card = el.closest('main').getBoundingClientRect()
    return r.top - card.top
  })
  const slider = page.locator('input[type="range"]')
  const plot = page.locator('[data-mesh-diff]')
  const reset = page.getByRole('button', { name: 'Reset view', exact: true })
  const settled = () => page.waitForFunction(() => getComputedStyle(document.querySelector('main')).marginLeft === '0px')
  await page.setViewportSize({ width: 390, height: 900 })
  await settled()
  const runOn = await box(run)
  await change.click()
  const runOff = await box(run)
  assert.ok(Math.abs(runOn - runOff) < 1, `calibration button moved ${runOn - runOff}px`)
  await change.click()
  await page.setViewportSize({ width: 561, height: 900 })
  await settled()
  const sliderOn = await box(slider), plotOn = await box(plot), resetOn = await box(reset)
  await change.click()
  assert.ok(Math.abs(sliderOn - (await box(slider))) < 1, `Z scale moved ${sliderOn - (await box(slider))}px`)
  assert.ok(Math.abs(plotOn - (await box(plot))) < 1, `plot moved ${plotOn - (await box(plot))}px`)
  assert.ok(Math.abs(resetOn - (await box(reset))) < 1, `Reset view moved ${resetOn - (await box(reset))}px`)
  await change.click()
  await page.setViewportSize({ width: 1440, height: 1000 })

  // The open comparison belongs to the run that just finished. Start the next one from the active profile,
  // or a later check would pass even if the result never opened itself.
  await profile.selectOption('active')
  assert.equal(await profile.inputValue(), 'active')

  // Confirm and leave Mesh before the running stage arrives. The result is still opened.
  let releasePlates = null
  let holdAllPlates = false
  await page.route('**/api/plates', async route => {
    if (route.request().method() === 'GET' && (releasePlates || holdAllPlates)) {
      const release = releasePlates
      if (!holdAllPlates) releasePlates = null
      if (release) await release
    }
    await route.continue()
  })
  const confirming = dialog.getByRole('button', { name: 'Confirm', exact: true })
  await run.click()
  let resolveHold
  releasePlates = new Promise(resolve => { resolveHold = resolve })
  const started = confirming.click()
  await page.evaluate(() => { location.hash = '#bed/plates' })
  resolveHold()
  await started
  const finished = () => page.waitForResponse(async r => {
    if (!r.url().includes('/api/plates') || r.request().method() !== 'GET') return false
    const data = await r.json()
    return data.calibration?.result === 'saved' && data.calibration?.stage === 'off'
  })
  let saved = finished()
  for (let i = 0; i < 5; i++) await step()
  await saved
  await page.getByRole('tab', { name: 'Mesh & Saved Profiles', exact: true }).click()
  assert.equal(await profile.inputValue(), 'measure:aa00000000000080', 'a run finished on another tab still opens its mesh')
  assert.equal(await change.getAttribute('aria-pressed'), 'true')

  // Mesh has seen the run; it ends on Build Plates. View mesh on the other row must keep that row.
  await run.click()
  await confirming.click()
  for (const stage of ['heating', 'soaking', 'probing', 'saving']) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
  }
  await page.evaluate(() => { location.hash = '#bed/plates' })
  saved = finished()
  await step()
  await saved
  await page.locator('[data-measure="aa00000000000060"]').getByRole('button', { name: 'View mesh', exact: true }).click()
  assert.equal(await profile.inputValue(), 'measure:aa00000000000060')
  await page.waitForTimeout(400)
  assert.equal(await profile.inputValue(), 'measure:aa00000000000060', 'View mesh is not replaced by the finished run')
  await page.locator('[data-mesh-diff="off"]').waitFor()

  // View mesh before the first reply. No later switch: a snapshot taken at heating would already be 60 °C.
  await profile.selectOption('active')
  await run.click()
  let resolveEarly
  releasePlates = new Promise(resolve => { resolveEarly = resolve })
  const early = confirming.click()
  await page.waitForFunction(() => !document.querySelector('[role="dialog"]'))
  await page.evaluate(() => { location.hash = '#bed/plates' })
  await page.locator('[data-measure="aa00000000000060"]').getByRole('button', { name: 'View mesh', exact: true }).click()
  resolveEarly()
  await early
  for (const stage of ['heating', 'soaking', 'probing', 'saving']) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
  }
  saved = finished()
  await step()
  await saved
  await page.waitForTimeout(400)
  assert.equal(await profile.inputValue(), 'measure:aa00000000000060', 'View mesh before heating stays selected')
  await page.locator('[data-mesh-diff="off"]').waitFor()

  // The run is already on screen at active. Leave and come back to active: that is still a choice.
  await profile.selectOption('active')
  await run.click()
  await confirming.click()
  await page.locator('[data-calibration="homing"]').waitFor()
  await profile.selectOption('measure:aa00000000000060')
  await profile.selectOption('active')
  for (const stage of ['heating', 'soaking', 'probing', 'saving']) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
  }
  saved = finished()
  await step()
  await saved
  await page.waitForTimeout(400)
  assert.equal(await profile.inputValue(), 'active', 'returning to the profile where the run started keeps it')

  // No active stage reaches the page: Confirm, leave for the dashboard, and the run is already saved on return.
  await profile.selectOption('active')
  let releaseMiss
  holdAllPlates = true
  releasePlates = new Promise(resolve => { releaseMiss = resolve })
  await run.click()
  const missed = confirming.click()
  await page.waitForFunction(() => !document.querySelector('[role="dialog"]'))
  await page.evaluate(() => { location.hash = '#dashboard' })
  for (let i = 0; i < 5; i++) await step()
  holdAllPlates = false
  releaseMiss()
  await missed
  await page.evaluate(() => { location.hash = '#bed' })
  await page.waitForFunction(() => {
    const label = [...document.querySelectorAll('label')].find(l => l.textContent.includes('Mesh profile'))
    return label?.querySelector('select')?.value === 'measure:aa00000000000080'
  })
  assert.equal(await profile.inputValue(), 'measure:aa00000000000080', 'a run whose stages were all missed still opens')
  assert.equal(await change.getAttribute('aria-pressed'), 'true')

  // A rejected start must not keep its generation. The next run, begun elsewhere, still opens.
  await profile.selectOption('active')
  let rejectStart = true
  await page.route('**/api/plates/calibrate', async route => {
    if (rejectStart && route.request().method() === 'POST') {
      rejectStart = false
      return route.fulfill({ status: 409, contentType: 'application/json', body: '{"error":"busy"}' })
    }
    return route.continue()
  })
  await run.click()
  await confirming.click()
  await page.getByText('busy', { exact: false }).waitFor()
  await profile.selectOption('measure:aa00000000000060')
  await page.evaluate(async () => {
    location.hash = '#dashboard'
    await fetch('/api/plates/calibrate', {
      method: 'POST', headers: { 'X-CC2-Request': '1', 'Content-Type': 'text/plain' },
      body: 'A\n80\n0\ndd00000000000004\na1b2c3d4e5f60718',
    })
    location.hash = '#bed'
  })
  await page.locator('[data-calibration="homing"]').waitFor()
  for (const stage of ['heating', 'soaking', 'probing', 'saving']) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
  }
  saved = finished()
  await step()
  await saved
  await page.waitForTimeout(400)
  assert.equal(await profile.inputValue(), 'measure:aa00000000000080', 'a run after a rejected start still opens')
  assert.equal(await change.getAttribute('aria-pressed'), 'true')

  // Opening Mesh sends two reads. Both still carry the previous off/saved and arrive after homing.
  const previous = await page.evaluate(async () => (await fetch('/api/plates')).text())
  await temperature.fill('60')
  await profile.selectOption('active')
  const delayed = []
  let delayGets = 2
  let bothDelayed = () => {}
  const delayedReady = new Promise(resolve => { bothDelayed = resolve })
  await page.route('**/api/plates', async route => {
    if (delayGets > 0 && route.request().method() === 'GET') {
      delayGets--
      delayed.push(route)
      if (!delayGets) bothDelayed()
      return
    }
    return route.fallback()
  })
  await page.evaluate(() => { location.hash = '#bed/plates' })
  await page.evaluate(() => { location.hash = '#bed' })
  let heldBoth = false
  await Promise.race([delayedReady.then(() => { heldBoth = true }), page.waitForTimeout(5000)])
  if (!heldBoth) throw new Error(`held ${delayed.length} plate reads before the run`)
  await run.click()
  await confirming.click()
  await page.locator('[data-calibration="homing"]').waitFor()
  for (const route of delayed) await route.fulfill({ status: 200, contentType: 'application/json', body: previous })
  await page.waitForTimeout(300)
  await page.locator('[data-calibration="homing"]').waitFor()
  assert.equal(await profile.inputValue(), 'active', 'two late saves do not open the previous mesh')
  for (const stage of ['heating', 'soaking', 'probing', 'saving']) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
  }
  saved = finished()
  await step()
  await saved
  await page.waitForTimeout(400)
  assert.equal(await profile.inputValue(), 'measure:aa00000000000060', 'the run that was on screen opens its own mesh')
  await temperature.fill('80')

  // Mesh is closed while another client saves 80 °C. This page starts 60 °C before those reads return,
  // and the next read fails. The late 80 °C library must not take the place of the 60 °C result.
  const dashboardRead = page.waitForResponse(r => new URL(r.url()).pathname === '/api/plates' && r.request().method() === 'GET')
  await page.evaluate(() => { location.hash = '#dashboard' })
  await dashboardRead
  const heldLate = []
  let holdLate = true
  let failNextPlates = false
  let failHeld = null
  let failedRead = () => {}
  const failedReady = new Promise(resolve => { failedRead = resolve })
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    if (holdLate) return heldLate.push(route)
    if (failNextPlates) {
      failNextPlates = false
      failHeld = route
      failedRead()
      return
    }
    return route.fallback()
  })
  await page.evaluate(async () => {
    const response = await fetch('/api/plates/calibrate', {
      method: 'POST', headers: { 'X-CC2-Request': '1', 'Content-Type': 'text/plain' },
      body: 'A\n80\n0\ndd00000000000004\na1b2c3d4e5f60718',
    })
    if (!response.ok) throw new Error(`other client could not start: ${response.status}`)
  })
  for (let i = 0; i < 5; i++) await step()
  const foreign = await (await page.request.get(`${origin}/api/plates`)).text()
  const beforeOpen = heldLate.length
  await page.evaluate(() => { location.hash = '#bed' })
  await temperature.waitFor()
  let stopWait = false
  const gotLate = await Promise.race([
    (async () => {
      while (!stopWait && heldLate.length < beforeOpen + 2) await page.waitForTimeout(20)
      return heldLate.length >= beforeOpen + 2
    })(),
    page.waitForTimeout(5000).then(() => false),
  ])
  stopWait = true
  if (!gotLate) throw new Error(`held ${heldLate.length - beforeOpen} reads from opening Mesh`)
  await temperature.fill('60')
  await profile.selectOption('active')
  holdLate = false
  failNextPlates = true
  await run.click()
  await confirming.click()
  let sawFail = false
  await Promise.race([failedReady.then(() => { sawFail = true }), page.waitForTimeout(8000)])
  if (!sawFail) throw new Error('the read after the start did not fail')
  // The saved mesh from before the run arrives while the fresh read is still waiting, then the fresh read fails.
  await heldLate[0].fulfill({ status: 200, contentType: 'application/json', body: foreign })
  await page.waitForTimeout(300)
  assert.equal(await profile.inputValue(), 'active', 'an older save does not finish a run whose newer read is still waiting')
  await failHeld.fulfill({ status: 503, contentType: 'application/json', body: '{"error":"unavailable"}' })
  for (const route of heldLate.slice(1)) await route.fulfill({ status: 200, contentType: 'application/json', body: foreign })
  await page.waitForTimeout(300)
  assert.equal(await profile.inputValue(), 'active', 'a mesh saved while this page was away is not the run just started')
  for (let i = 0; i < 5; i++) await step()
  await page.evaluate(() => { location.hash = '#bed/plates' })
  await page.evaluate(() => { location.hash = '#bed' })
  await page.waitForFunction(() => {
    const label = [...document.querySelectorAll('label')].find(l => l.textContent.includes('Mesh profile'))
    return label?.querySelector('select')?.value === 'measure:aa00000000000060'
  })
  await temperature.fill('80')

  // The printer has not accepted the 60 °C run yet. An older save, including one read after Confirm
  // on the way to Build Plates, must not take its place. The real result still opens after missed stages.
  await profile.selectOption('active')
  await temperature.fill('60')
  let plateMode = 'hold'
  let earlierSave = ''
  const queued = []
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    if (plateMode === 'hold') return void queued.push(route)
    if (plateMode === 'foreign')
      return route.fulfill({ status: 200, contentType: 'application/json', body: earlierSave })
    return route.fallback()
  })
  await page.evaluate(async () => {
    const response = await fetch('/api/plates/calibrate', {
      method: 'POST', headers: { 'X-CC2-Request': '1', 'Content-Type': 'text/plain' },
      body: 'A\n80\n0\ndd00000000000004\na1b2c3d4e5f60718',
    })
    if (!response.ok) throw new Error(`other client could not start: ${response.status}`)
  })
  for (let i = 0; i < 5; i++) await step()
  earlierSave = await (await page.request.get(`${origin}/api/plates`)).text()
  let pauseStart = true
  let pausedStart = null
  let sawStart = () => {}
  const startPaused = new Promise(resolve => { sawStart = resolve })
  await page.route('**/api/plates/calibrate', async route => {
    if (pauseStart && route.request().method() === 'POST') {
      pauseStart = false
      pausedStart = route
      sawStart()
      return
    }
    return route.fallback()
  })
  await run.click()
  await confirming.click()
  let sawPaused = false
  await Promise.race([startPaused.then(() => { sawPaused = true }), page.waitForTimeout(8000)])
  if (!sawPaused) throw new Error('the start request was not held')
  plateMode = 'foreign'
  for (const route of queued.splice(0))
    await route.fulfill({ status: 200, contentType: 'application/json', body: earlierSave })
  await page.waitForTimeout(300)
  assert.equal(await profile.inputValue(), 'active', 'a save that arrives before the printer accepts does not finish the run')
  await page.evaluate(() => { location.hash = '#bed/plates' })
  await page.waitForTimeout(300)
  await page.evaluate(() => { location.hash = '#bed' })
  await profile.waitFor()
  assert.equal(await profile.inputValue(), 'active', 'a read sent after Confirm is not the run while the start is still open')
  plateMode = 'hold'
  await pausedStart.fallback()
  for (let i = 0; i < 5; i++) await step()
  plateMode = 'live'
  for (const route of queued.splice(0)) await route.fallback()
  await page.evaluate(() => { location.hash = '#bed/plates' })
  await page.evaluate(() => { location.hash = '#bed' })
  await page.waitForFunction(() => {
    const label = [...document.querySelectorAll('label')].find(l => l.textContent.includes('Mesh profile'))
    return label?.querySelector('select')?.value === 'measure:aa00000000000060'
  })
  await temperature.fill('80')

  // The server can start the run even if the browser loses its acknowledgement. The run ID also
  // arrives in the saved result, so missing every active stage must not lose the comparison.
  await profile.selectOption('active')
  await temperature.fill('60')
  plateMode = 'hold'
  await page.route('**/api/plates/calibrate', async route => {
    const accepted = await route.fetch()
    assert.equal(accepted.status(), 202)
    await route.abort()
  }, { times: 1 })
  await run.click()
  const lostStart = page.waitForEvent('requestfailed', r => new URL(r.url()).pathname === '/api/plates/calibrate')
  await confirming.click()
  const lost = await lostStart
  const runId = lost.headers()['x-cc2-calibration-run']
  assert.match(runId, /^[0-9a-f]{16}$/)
  await page.evaluate(() => { location.hash = '#dashboard' })
  await profile.waitFor({ state: 'detached' })
  for (let i = 0; i < 5; i++) await step()
  const afterLostStart = await (await page.request.get(origin + '/api/plates')).json()
  assert.equal(afterLostStart.calibration.run, runId)
  assert.equal(afterLostStart.calibration.result, 'saved')
  plateMode = 'live'
  for (const route of queued.splice(0)) await route.fallback()
  await page.evaluate(() => { location.hash = '#bed' })
  await page.locator('[data-mesh-diff="on"]').waitFor()
  assert.equal(await profile.inputValue(), 'measure:aa00000000000060', 'a lost start response still opens its own result')
  await temperature.fill('80')

  // Homed, no plate, no soak: the confirmation says less; stopping while heating switches the bed off.
  // The header polls the printer every 3 s, so wait for a reply that already carries the new axes.
  homed = 'xyz'
  await page.waitForResponse(r => new URL(r.url()).pathname === '/api/printer')
  await page.waitForTimeout(200)
  await soak.selectOption('0')
  await target.selectOption('')
  plateCalls.length = 0
  await run.click()
  await dialog.getByText('Start a new bed mesh calibration at 80 °C?', { exact: false }).waitFor()
  for (const line of ['The printer is not homed', 'is held there for', 'The result becomes'])
    assert.equal(await dialog.getByText(line, { exact: false }).count(), 0, line)
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await progress.waitFor()
  assert.deepEqual(plateCalls, [['/api/plates/calibrate', 'A\n80\n0\ndd00000000000004\n']])
  await step()
  await page.locator('[data-calibration-bar="heating"]').waitFor()
  await progress.getByRole('button', { name: 'Stop', exact: true }).click()
  await toast('The calibration was stopped and the bed heater switched off.')
  assert.equal(plateCalls.at(-1)[0], '/api/plates/calibrate/cancel')
  await progress.waitFor({ state: 'detached' })

  // Two finishes that save nothing. The second off/done matches the snapshot from its own start.
  const finishUnsaved = async () => {
    await run.click()
    await confirming.click()
    await page.locator('[data-calibration="homing"]').waitFor()
    for (const stage of ['heating', 'soaking', 'probing']) {
      await step()
      await page.locator(`[data-calibration="${stage}"]`).waitFor()
    }
    await step()
    await progress.waitFor({ state: 'detached' })
    await toast('The bed mesh calibration finished.')
  }
  await finishUnsaved()
  await temperature.fill('60')
  let platesGets = 0
  const countPlates = r => {
    if (r.method() === 'GET' && new URL(r.url()).pathname === '/api/plates') platesGets++
  }
  await run.click()
  await confirming.click()
  await page.locator('[data-calibration="homing"]').waitFor()
  for (const stage of ['heating', 'soaking', 'probing']) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
  }
  page.on('request', countPlates)
  await step()
  await progress.waitFor({ state: 'detached' })
  await toast('The bed mesh calibration finished.')
  await page.waitForTimeout(2600)
  assert.ok(platesGets < 8, `a second unsaved finish polled /api/plates ${platesGets} times`)
  page.off('request', countPlates)
  await temperature.fill('80')

  // A run that fails while homing says so.
  await run.click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await progress.waitFor()
  await step('fail')
  await toast('Homing did not finish, so the calibration was not started.')
  assert.deepEqual(errors, [])
  console.log('PASS: mesh calibration runs on CC2 Control with temperature, soak, nozzle and plate; stages, stop, outcome and mesh reload')
} finally { await browser?.close(); await server.close() }
