import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
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
  const start = card.getByRole('button', { name: 'Start session', exact: true })
  const check = card.getByRole('button', { name: 'Check all four', exact: true })
  const end = card.getByRole('button', { name: 'End session', exact: true })
  const measure = card.getByRole('button', { name: 'Measure Screws', exact: true })
  const fl = card.getByRole('button', { name: 'Re-probe FL', exact: true })
  await start.waitFor()
  assert.ok(await check.isDisabled(), 'check is disabled before a session')
  assert.ok(await end.isDisabled(), 'end is disabled before a session')
  assert.ok(await fl.isDisabled(), 'corner re-probe is disabled before a session')
  assert.ok(await measure.isEnabled(), 'one-shot measurement stays available')
  assert.deepEqual(
    await page.evaluate(async () => (await (await fetch('/api/printer')).json()).screws_session),
    { active: false, xy_homed: false }
  )
  await page.screenshot({ path: path.join(shots, 'screw-tram-idle-1440.png'), fullPage: true })
  await start.click()
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
  await page.screenshot({ path: path.join(shots, 'screw-tram-session-1440.png'), fullPage: true })
  const probed = posted(page, 'screws:tram:probe:FL:2')
  await fl.click()
  await probed
  await card.getByText(/drift/).waitFor()
  assert.equal(actions.at(-1), 'screws:tram:probe:FL:2', 'default probe-twice sends :2')
  const checked = posted(page, 'screws:tram:check:2')
  await check.click()
  await confirm(page).click()
  await checked
  assert.equal(actions.at(-1), 'screws:tram:check:2')
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
  assert.deepEqual(errors, [])
  console.log('PASS screw-tram session start, one-click corner re-probe, four-corner check, end, and layout')
} finally {
  await browser?.close()
  await server.close()
}
