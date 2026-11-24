// Bed levelling, Mesh tab: the buttons under the mesh follow an open screw session (greyed out while one of its measurements runs,
// the start question says that the session ends first). The component must call every hook on every render, whether or not a
// session is open: a hook behind `sessionOpen &&` moves the hooks after it when the session opens or closes.
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
    if (r.method() === 'POST' && new URL(r.url()).pathname.startsWith('/api/plates/calibrate')) posts.push(new URL(r.url()).pathname)
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()

  // The simulated service: the session and the console are switched by the test (a measurement of the session runs or not).
  let sessionOpen = false, consoleBusy = false
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.screws_session = { active: sessionOpen, xy_homed: true, bed: 60 }
    await route.fulfill({ response, json: data })
  })
  await page.route(/\/api\/console(\?.*)?$/, route => (route.request().method() === 'GET' ? route.fulfill({ json: { busy: consoleBusy, completed: !consoleBusy, success: true, generation: 700, command: '', output: '' } }) : route.fallback()))
  await page.route('**/api/plates/calibrate', route => (route.request().method() === 'POST' ? route.fulfill({ status: 202, json: { accepted: true } }) : route.fallback()))

  // Count the hook calls of the buttons component: it asks the console status store with `consoleStatus.use()` on every render.
  // The stack of the call names the component (the dev server keeps the function names).
  await page.goto(`${origin}/#bed/mesh`)
  const startButton = page.getByRole('button', { name: /Run Bed Mesh Calibration/ })
  await startButton.waitFor()
  await page.evaluate(async () => {
    const { consoleStatus } = await import('/src/lib/state.ts')
    const w = window
    w.__hookCalls = { MeshActions: 0, other: 0 }
    const real = consoleStatus.use
    consoleStatus.use = function () {
      const from = new Error().stack?.includes('MeshActions') ? 'MeshActions' : 'other'
      w.__hookCalls[from]++
      return real.apply(this, arguments)
    }
  })
  const calls = () => page.evaluate(() => window.__hookCalls.MeshActions)
  const side = page.locator('label', { hasText: 'Calibration plate side' }).locator('select')
  await side.selectOption('default')
  const blocked = page.locator('[data-cal-blocked]')

  // (a) No session: the component still asks the console status on every render.
  const before = await calls()
  await side.selectOption('default1') // a render without any change of the session
  await side.selectOption('default')
  const afterClosed = await calls()
  assert.ok(afterClosed > before, `no session: the buttons component calls the console status hook on every render (${before} -> ${afterClosed})`)
  assert.ok(await startButton.isEnabled(), 'no session: the start is free')

  // (c) A session is open and one of its measurements runs: the start is greyed out with the reason.
  sessionOpen = true
  consoleBusy = true
  await blocked.getByText('A measurement of the screw session is running; wait until it is done.').waitFor({ timeout: 15000 })
  assert.ok(await startButton.isDisabled(), 'a session measurement runs: the start is greyed out')

  // (b) The session stays open between its measurements: the start asks, says that the session ends first and sends the start.
  consoleBusy = false
  await blocked.waitFor({ state: 'detached', timeout: 15000 })
  assert.ok(await startButton.isEnabled(), 'session open, no measurement: the start is free')
  const mark = await calls()
  await side.selectOption('default1')
  await side.selectOption('default')
  assert.ok((await calls()) > mark, 'session open: the console status hook is called on every render too')
  await startButton.click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('The running screw session ends first.').waitFor({ timeout: 10000 })
  posts.length = 0
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await new Promise(r => setTimeout(r, 400))
  assert.deepEqual(posts, ['/api/plates/calibrate'], 'the start is sent while the session is open')

  // The session ends by itself: the start question does not mention it any more, and a start is sent again.
  sessionOpen = false
  await page.waitForFunction(() => !document.querySelector('[data-cal-blocked]'))
  await page.waitForTimeout(6000) // the page reads the printer again
  await side.selectOption('default1')
  await side.selectOption('default')
  await startButton.click()
  await dialog.waitFor({ timeout: 10000 })
  assert.equal(await dialog.getByText('The running screw session ends first.').count(), 0, 'no session: the question says nothing about it')
  posts.length = 0
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await new Promise(r => setTimeout(r, 400))
  assert.deepEqual(posts, ['/api/plates/calibrate'], 'the start is sent after the session ended')

  // The session opens again while a measurement runs: greyed out again (the hooks stay in order over several changes).
  sessionOpen = true
  consoleBusy = true
  await blocked.waitFor({ timeout: 15000 })
  assert.ok(await startButton.isDisabled(), 'a second session: the start is greyed out again')
  assert.deepEqual(errors, [])
  console.log('PASS bed session hook: the buttons call the console status hook on every render, session closed or open; the start follows the session (greyed out while it measures, sent between its measurements and after it ended)')
} finally {
  await browser?.close()
  await server.close()
}
