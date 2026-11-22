// The session bar: it stands on every page while a screw session runs, also on Bed alignment (there without "Go to the
// session"); "End session" is only here (step 2 has none) and is greyed out while a measurement runs.
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
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.goto(`${origin}/#bed/align`)
  const card = page.locator('#screwFocus')
  await card.waitFor()
  const bar = page.locator('[data-level-session-bar]')
  assert.equal(await bar.count(), 0, 'no bar without a session')
  await card.locator('[data-level-start-mode-button="session"]').click()
  // The hold time of step 1 goes in front of every start the user makes; 0 here (test-bed-mesh-tick covers it).
  await card.locator('[data-level-soak]').selectOption('0')
  await card.getByRole('button', { name: /Start session/ }).click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await bar.waitFor({ timeout: 10000 })
  // On Bed alignment: the bar is there, without "Go to the session"; "End session" is in the bar only.
  await bar.getByText('The screw session is still running').waitFor()
  assert.equal(await bar.getByRole('button', { name: /Go to the session/ }).count(), 0, 'no "Go to the session" on Bed alignment')
  assert.equal(await card.getByRole('button', { name: /End session/ }).count(), 0, 'step 2 has no End session')
  const end = bar.getByRole('button', { name: /End session/ })
  await page.waitForFunction(() => document.querySelector('[data-session-bar-end]') && !document.querySelector('[data-session-bar-end]').disabled, null, { timeout: 15000 })
  // On another page: with "Go to the session".
  await page.evaluate(() => (location.hash = '#dashboard'))
  await bar.getByRole('button', { name: /Go to the session/ }).waitFor()
  assert.match(await bar.getByRole('button', { name: /Go to the session/ }).getAttribute('class'), /border-amber/, 'the way back has the orange of the session')
  assert.ok(await end.isEnabled(), 'End session works when nothing runs')
  await bar.getByRole('button', { name: /Go to the session/ }).click()
  await card.waitFor()
  assert.equal(await bar.getByRole('button', { name: /Go to the session/ }).count(), 0)
  // "Measure again" is the accent-coloured main action once the session has measured; without a measurement it stays plain.
  const again = card.locator('[data-screw-check]')
  await again.waitFor()
  await page.waitForFunction(() => document.querySelector('[data-screw-check]')?.className.includes('bg-cyan'), null, { timeout: 15000 }) // a measurement was made: highlighted
  await page.route(/\/api\/console(\?.*)?$/, route => (route.request().method() === 'GET' ? route.fulfill({ json: { busy: false, completed: false, success: false, generation: 1, command: '', output: '' } }) : route.fallback()))
  await page.route('**/api/screws/history', route => (route.request().method() === 'GET' ? route.fulfill({ json: { sessions: [] } }) : route.fallback()))
  await page.reload()
  await again.waitFor()
  assert.doesNotMatch(await again.getAttribute('class'), /(^|\s)bg-cyan/, 'no measurement yet: Measure again stays as it was')
  assert.equal(await again.innerText(), 'Measure again')
  const widthOfText = (await again.boundingBox()).width
  assert.ok(widthOfText < 220, 'only as wide as its text')
  await page.unroute(/\/api\/console(\?.*)?$/)
  await page.unroute('**/api/screws/history')
  await page.reload()
  await again.waitFor()
  await page.waitForFunction(() => document.querySelector('[data-screw-check]')?.className.includes('bg-cyan'), null, { timeout: 15000 })
  // While a measurement runs the end button is greyed out (the same condition as the other screw buttons).
  await page.route(/\/api\/console(\?.*)?$/, route => (route.request().method() === 'GET' ? route.fulfill({ json: { busy: true, completed: false, success: false, generation: 900, command: '', output: '' } }) : route.fallback()))
  await page.waitForFunction(() => document.querySelector('[data-session-bar-end]')?.disabled === true, null, { timeout: 15000 })
  // While a measurement runs the idle limit stands still: no countdown, the bar says so; after it the countdown is back.
  await bar.locator('[data-session-bar-paused]').getByText('the time limit stands still while a measurement runs').waitFor()
  assert.equal(await bar.getByText(/ends in about \d+ min/).count(), 0, 'no countdown during a measurement')
  await page.unroute(/\/api\/console(\?.*)?$/)
  await page.waitForFunction(() => document.querySelector('[data-session-bar-end]')?.disabled === false, null, { timeout: 15000 })
  // 150 % text at 320 px: no sideways scrolling.
  await page.setViewportSize({ width: 320, height: 700 })
  await page.evaluate(() => (document.documentElement.style.fontSize = '150%'))
  await page.waitForTimeout(150)
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'the bar causes no sideways scrolling at 150 %')
  await page.evaluate(() => (document.documentElement.style.fontSize = ''))
  await page.setViewportSize({ width: 1440, height: 1000 })
  // End from the bar.
  await end.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await bar.waitFor({ state: 'detached', timeout: 15000 })
  assert.deepEqual(errors, [])
  console.log('PASS session bar: also on Bed alignment, Go to the session only elsewhere, End session only here and greyed out while measuring')
} finally {
  await browser?.close()
  await server.close()
}
