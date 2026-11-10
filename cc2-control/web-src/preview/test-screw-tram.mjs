import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

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
  await card.getByRole('checkbox', { name: '2× 3 probes', exact: true }).waitFor()
  await card.getByText('has not settled yet').waitFor()
  await card.getByText('6 per corner').waitFor()
  await card.getByText('Mesh capture stays separate.').waitFor()
  assert.equal(await card.getByText('both heaters turn off afterwards').count(), 0)
  const history = card.locator('#screw-history')
  await history.getByText('Earlier sessions', { exact: true }).click()
  await history.getByText('−188 µm').waitFor()
  await history.getByText('Front right +6 µm').waitFor()
  assert.deepEqual(
    await page.evaluate(async () => (await (await fetch('/api/printer')).json()).screws_session),
    { active: false, xy_homed: false }
  )
  await page.screenshot({ path: path.join(shots, 'screw-tram-idle-1440.png'), fullPage: true })
  await start.click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('only the nozzle turns off').waitFor()
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
  await card.getByText('+50 µm').first().waitFor()
  await card.getByText('+50 µm').nth(1).waitFor()
  assert.equal(await card.getByText(/vs last/).count(), 0, 'the saved trail replaces the previous delta')
  await page.screenshot({ path: path.join(shots, 'screw-tram-session-1440.png'), fullPage: true })
  const probed = posted(page, 'screws:tram:probe:FL:2')
  await fl.click()
  await probed
  await card.getByText('1st→2nd: −1 µm').waitFor()
  await card.getByText('other values are out of date').waitFor()
  assert.equal(actions.at(-1), 'screws:tram:probe:FL:2', 'default probe-twice sends :2')
  const checked = posted(page, 'screws:tram:check:2')
  await check.click()
  await confirm(page).click()
  await checked
  assert.equal(actions.at(-1), 'screws:tram:check:2')
  await card.getByText('+50 → +71 µm').waitFor()
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
  const csvDownload = page.waitForEvent('download')
  await history.getByRole('button', { name: 'Export CSV', exact: true }).click()
  const csvFile = await csvDownload
  assert.equal(csvFile.suggestedFilename(), 'screw-levelling-history.csv')
  assert.match(await readFile(await csvFile.path(), 'utf8'), /FL_um,FR_um/)
  const cleared = page.waitForRequest(
    r => r.method() === 'POST' && r.url().endsWith('/api/screws/history/clear')
  )
  await history.getByRole('button', { name: 'Clear history', exact: true }).click()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(
    await page.evaluate(async () => (await (await fetch('/api/screws/history')).json()).sessions.length),
    2,
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
