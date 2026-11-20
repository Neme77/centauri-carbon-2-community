// The mesh calibration bar: while CC2 Control runs a bed mesh calibration (any mesh), a field in the accent colour stands at the top
// of every page of CC2 Control, on Bed levelling above the tabs and only once (what it does, the time left, "Stop" until the probing starts); the status field of the header reads
// "Mesh calibration" instead of Idle; the old message beside the buttons of the Mesh tab and in step 1 of Bed alignment is gone.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
let browser
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const errors = []
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  const posts = []
  page.on('request', r => {
    if (r.method() === 'POST' && new URL(r.url()).pathname.startsWith('/api/plates/')) posts.push(new URL(r.url()).pathname)
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  let stage = 'off'
  let probeSince = 0
  const job = { side: 'A', temp: 60, soak: 300, remaining: 272, elapsed: 0, bed: 58.4, plate: '', nozzle: '', measure: '', result: '', error: '', detail: '' }
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const lib = await (await route.fetch()).json()
    // Like the program: the seconds since the stage began grow on their own (probing began 42 s before the test looked).
    lib.calibration = { ...lib.calibration, ...job, stage, elapsed: probeSince ? 42 + Math.floor((Date.now() - probeSince) / 1000) : 0 }
    return route.fulfill({ json: lib })
  })
  const bar = page.locator('[data-calibration-bar]')
  const field = page.locator('[data-mesh-status]')
  // Leave Bed levelling and wait until its page is gone, so that entering it again mounts it anew (and reads the library).
  const leave = async () => {
    await page.goto(`${origin}/#dashboard`)
    await page.waitForFunction(() => !document.querySelector('[role="tablist"]'))
  }
  const header = page.locator('.cc2-header-status')
  await page.goto(`${origin}/#bed/mesh`)
  await page.locator('[role="tab"]').first().waitFor()
  assert.equal(await bar.count(), 0, 'no bar without a calibration')

  // Soaking: the bar on every tab, above the tabs, accent colour; the time left; Stop.
  stage = 'soaking'
  for (const hash of ['mesh', 'align', 'plates', 'nozzle']) {
    await leave() // entering the page again reads the library at once
    await page.goto(`${origin}/#bed/${hash}`)
    await bar.waitFor({ timeout: 15000 })
    assert.equal(await bar.getAttribute('data-calibration-bar'), 'soaking')
    await bar.getByText('Mesh calibration running', { exact: true }).waitFor()
    await bar.getByText('Holding the bed at 60 °C: 4:32 left').waitFor()
    const b = await bar.boundingBox(), tabs = await page.locator('[role="tablist"]').boundingBox()
    assert.ok(b.y + b.height <= tabs.y + 1, `the bar is above the tabs (${hash})`)
    assert.match(await field.getAttribute('class'), /border-cyan/, 'accent colour, not the orange of the session bar')
    assert.equal(await bar.count(), 1, `one calibration line, none under the heading of the page (${hash})`)
    assert.equal(await field.count(), 1)
    assert.equal(await bar.getAttribute('data-calibration-compact'), null, 'alone in the field: the normal size')
    // The way back to the tab the mesh is started in, in the colour of the field; not on that tab.
    assert.equal(await field.locator('[data-mesh-status-open="mesh"]').count(), hash === 'mesh' ? 0 : 1, `the way to the Mesh tab (${hash})`)
    await bar.getByRole('button', { name: 'Stop', exact: true }).waitFor()
    assert.equal(await page.locator('[data-calibration]').count(), 0, 'the old message beside the buttons is gone')
    assert.equal(await page.getByText('Runs on CC2 Control').count(), 0)
  }
  // Every other page of CC2 Control shows the same line with Stop at the top, at the place of the chain's bar.
  for (const hash of ['dashboard', 'files', 'settings']) {
    await page.goto(`${origin}/#${hash}`)
    await bar.waitFor({ timeout: 20000 })
    await bar.getByText('Holding the bed at 60 °C', { exact: false }).waitFor()
    await bar.getByRole('button', { name: 'Stop', exact: true }).waitFor()
    assert.equal(await bar.count(), 1, `one line (${hash})`)
    const f = await field.boundingBox(), main = await page.locator('.cc2-page-content').boundingBox()
    assert.ok(f.y - main.y < 4, `the field is the first thing of the page (${hash})`)
  }
  const back = field.locator('[data-mesh-status-open="mesh"]')
  await back.getByText('Mesh & Saved Profiles', { exact: true }).waitFor()
  assert.match(await back.getAttribute('class'), /border-cyan/, 'the button has the colour of the field')
  await back.click()
  await page.waitForFunction(() => location.hash === '#bed')
  await page.locator('[role="tablist"]').waitFor()
  await back.waitFor({ state: 'detached' })
  await page.goto(`${origin}/#bed/mesh`)
  await bar.waitFor()
  await header.getByText('Mesh calibration', { exact: true }).waitFor({ timeout: 8000 })
  assert.equal(await header.getByText('Idle').count(), 0, 'the status field says Mesh calibration instead of Idle')

  // Stop: the same action as before.
  await bar.getByRole('button', { name: 'Stop', exact: true }).click()
  await page.waitForFunction(() => true)
  assert.ok(posts.includes('/api/plates/calibrate/cancel'), 'Stop sends the calibration cancel')

  // The other stages: homing and heating can be stopped, probing and saving cannot.
  for (const [next, text, stoppable] of [
    ['homing', 'Homing X, Y and Z…', true],
    ['heating', 'Heating the bed: 58 °C of 60 °C', true],
    ['probing', 'Probing Side A at 60 °C…', false],
  ]) {
    stage = next
    await leave()
    await page.goto(`${origin}/#bed/plates`)
    await bar.waitFor({ timeout: 15000 })
    await bar.getByText(text).waitFor()
    assert.equal(await bar.getByRole('button', { name: 'Stop', exact: true }).count(), stoppable ? 1 : 0, `Stop at ${next}`)
  }

  // The measurement of the probing counts up from 0:00 at its start; the program reports the seconds, a reloaded page carries on.
  stage = 'probing'
  probeSince = Date.now()
  await leave()
  await page.goto(`${origin}/#bed/mesh`)
  const elapsed = bar.locator('[data-calibration-elapsed]')
  await elapsed.waitFor({ timeout: 15000 })
  await page.waitForFunction(() => /^0:4\d$/.test(document.querySelector('[data-calibration-elapsed]')?.textContent ?? ''), null, { timeout: 15000 })
  const first = await elapsed.innerText()
  assert.match(first, /^0:4[2-9]$/, `the count starts at the seconds of the program (got ${first})`)
  await page.waitForFunction(() => /^0:(4[5-9]|5\d)$/.test(document.querySelector('[data-calibration-elapsed]')?.textContent ?? ''), null, { timeout: 15000 })
  await page.reload()
  await elapsed.waitFor({ timeout: 15000 })
  assert.match(await elapsed.innerText(), /^0:4[2-9]|0:5\d$/, 'after a reload it carries on from the program')
  probeSince = 0
  assert.equal(await bar.locator('[data-calibration-stop]').count(), 0, 'no Stop while probing')

  // A screw session is open: while one of its measurements runs the start is greyed out with the reason; between its measurements
  // the start is free and its question says that the session ends first.
  stage = 'off'
  let sessionOpen = true, consoleBusy = true
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.screws_session = { active: sessionOpen, xy_homed: true, bed: 60 }
    await route.fulfill({ response, json: data })
  })
  await page.route('**/api/console', route => (route.request().method() === 'GET' ? route.fulfill({ json: { busy: consoleBusy, completed: !consoleBusy, success: true, generation: 700, command: '', output: '' } }) : route.fallback()))
  await page.route('**/api/plates/calibrate', route => (route.request().method() === 'POST' ? route.fulfill({ status: 202, json: { accepted: true } }) : route.fallback()))
  await leave()
  await page.goto(`${origin}/#bed/mesh`)
  const startButton = page.getByRole('button', { name: /Run Bed Mesh Calibration/ })
  await startButton.waitFor()
  await page.locator('[data-cal-blocked]').getByText('A measurement of the screw session is running; wait until it is done.').waitFor({ timeout: 15000 })
  assert.ok(await startButton.isDisabled(), 'a session measurement runs: the start is greyed out')
  consoleBusy = false
  await page.locator('[data-cal-blocked]').waitFor({ state: 'detached', timeout: 15000 })
  await page.locator('label', { hasText: 'Calibration plate side' }).locator('select').selectOption('default')
  await startButton.click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('The running screw session ends first.').waitFor()
  posts.length = 0
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => true)
  await new Promise(r => setTimeout(r, 300))
  assert.ok(posts.includes('/api/plates/calibrate'), 'the start is sent between the measurements of the session')
  await page.unroute('**/api/printer')
  await page.unroute('**/api/console')

  // A quick screw measurement (no session): it stands in the same field at the top of every page, blue, with its cancel
  // and the way to Bed alignment; the tab has no line of its own, and its badge is blue too.
  await page.route('**/api/console', route => (route.request().method() === 'GET' ? route.fulfill({ json: { busy: true, cancellable: true, completed: false, success: false, generation: 800, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: '' } }) : route.fallback()))
  await page.goto(`${origin}/#dashboard`)
  const measure = field.locator('[data-screw-measure-bar]')
  await measure.waitFor({ timeout: 20000 })
  await measure.getByText('Screw measurement running', { exact: true }).waitFor()
  await measure.locator('[data-screw-cancel]').waitFor()
  assert.doesNotMatch(await field.getAttribute('class'), /amber/, 'blue, not the orange of the session')
  assert.doesNotMatch(await measure.getAttribute('class'), /amber/)
  const toAlign = field.locator('[data-mesh-status-open="align"]')
  assert.match(await toAlign.getAttribute('class'), /border-cyan/)
  await toAlign.click()
  await page.waitForFunction(() => location.hash === '#bed/align')
  await page.locator('#screwFocus').waitFor()
  await toAlign.waitFor({ state: 'detached' })
  await measure.waitFor()
  assert.equal(await page.locator('[data-screw-status]').count(), 0, 'no line of the measurement in the tab')
  const badge = page.locator('#screwFocus').getByText('Probing…', { exact: true })
  await badge.waitFor()
  assert.match(await badge.getAttribute('class'), /border-cyan/, 'the badge of the card is blue outside a session')
  assert.equal(await page.locator('[data-screw-measure-bar]').count(), 1, 'one line of the measurement')
  await page.unroute('**/api/console')

  // 390 px and 150 % text: no sideways scrolling with the bar.
  await page.setViewportSize({ width: 390, height: 844 })
  stage = 'soaking'
  await leave()
  await page.goto(`${origin}/#bed/mesh`)
  await bar.waitFor({ timeout: 15000 })
  await page.evaluate(() => (document.documentElement.style.fontSize = '150%'))
  await page.waitForTimeout(200)
  const wide = await page.evaluate(() => (document.documentElement.scrollWidth <= innerWidth + 1 ? [] : [...document.querySelectorAll('main *')].filter(e => e.getBoundingClientRect().right > innerWidth + 1 && !e.closest('[role=tablist]')).slice(0, 5).map(e => `${e.tagName}.${String(e.className).slice(0, 40)}`)))
  assert.deepEqual(wide, [], 'no sideways scrolling with the bar at 150 %')
  await page.goto(`${origin}/#dashboard`)
  await bar.waitFor({ timeout: 20000 })
  await page.waitForTimeout(200)
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no sideways scrolling on the dashboard at 390 px')
  assert.deepEqual(errors, [])
  console.log('PASS calibration bar: on every page at the top (dashboard, files, settings), on every tab above the tabs and once, accent colour, time left, Stop, header status, old message gone, 150 % at 390 px')
} finally {
  await browser?.close()
  await server.close()
}
