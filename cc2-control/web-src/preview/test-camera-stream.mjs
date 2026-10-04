import assert from 'node:assert/strict'
import { createServer as httpServer } from 'node:http'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
const jpeg = Buffer.from('/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAQABADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDm6KKK+xPhD//Z', 'base64')
let clients = 0, requests = 0, reject = false
const stream = httpServer((req, res) => {
  requests++
  if (reject) { res.writeHead(503); res.end(); return }
  clients++
  res.writeHead(200, { 'Content-Type': 'multipart/x-mixed-replace; boundary=frame', 'Cache-Control': 'no-store' })
  const frame = () => { res.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`); res.write(jpeg); res.write('\r\n') }
  frame()
  const timer = setInterval(frame, 100)
  res.on('close', () => { clearInterval(timer); clients-- })
})
let browser
const wait = async (predicate, label) => {
  for (let i = 0; i < 100; i++) { if (predicate()) return; await new Promise(r => setTimeout(r, 50)) }
  throw new Error(label)
}
try {
  await new Promise(r => stream.listen(0, '127.0.0.1', r))
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } })
  const page = await context.newPage()
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await context.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await context.route('**/__preview/camera.svg*', r => r.fulfill({ status: 302, headers: { location: `http://127.0.0.1:${stream.address().port}/stream` } }))
  await page.goto(`${origin}/#dashboard`)
  const start = page.getByRole('button', { name: 'Start live view', exact: true })
  const pause = page.locator('.cc2-camera-frame').locator('..').getByRole('button', { name: 'Pause', exact: true })
  await start.waitFor()
  assert.equal(requests, 0, 'mount must not open a camera request')
  const begin = async () => { await start.click(); await wait(() => clients === 1, 'one stream must open') }
  await begin()
  await page.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  await pause.click()
  await wait(() => clients === 0, 'Pause must abort the actual MJPEG request')
  await begin()
  await page.goto(`${origin}/#control`)
  await wait(() => clients === 0, 'section change must close the stream')
  await page.goto(`${origin}/#dashboard`)
  await start.waitFor()
  assert.equal(clients, 0, 'returning to a page requires explicit start')
  await begin()
  // Real layout/IntersectionObserver path, without directly calling component functions.
  await page.setViewportSize({ width: 1440, height: 400 })
  await page.evaluate(() => window.scrollTo(0, document.body.scrollHeight))
  await wait(() => clients === 0, 'scrolling the camera out of view must close the stream')
  await page.setViewportSize({ width: 1440, height: 1000 })
  await start.scrollIntoViewIfNeeded()
  await begin()
  // Headless Chromium does not provide an interactive background tab. Exercise
  // visibilitychange in the browser, then verify cancellation at the HTTP server.
  await page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { configurable: true, value: true })
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await wait(() => clients === 0, 'hidden-tab event must close the stream')
  await page.evaluate(() => {
    delete document.hidden
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await start.waitFor()
  assert.equal(clients, 0, 'visible again must not restart streaming automatically')
  await begin()
  await page.goto('about:blank')
  await wait(() => clients === 0, 'leaving the page must close the stream')
  await page.goto(`${origin}/#dashboard`)
  await start.waitFor()
  // One viewer at a time: another page starting live view takes the stream, the first page hands it over
  // without reconnecting by itself, and Watch here takes it back.
  const other = await context.newPage()
  other.on('pageerror', e => errors.push(e.message))
  await other.goto(`${origin}/#dashboard`)
  const watchHere = page.getByRole('button', { name: 'Watch here', exact: true })
  const otherWatchHere = other.getByRole('button', { name: 'Watch here', exact: true })
  await begin()
  await other.getByRole('button', { name: 'Start live view', exact: true }).click()
  await other.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  await watchHere.waitFor()
  await page.getByText('Live view is open in another window or on another device', { exact: true }).waitFor()
  await wait(() => clients === 1, 'the previous viewer must close its stream')
  const handedOver = requests
  await new Promise(r => setTimeout(r, 4000))
  assert.equal(requests, handedOver, 'a page that handed the stream over must not reconnect by itself')
  assert.equal(clients, 1, 'only the new viewer streams')
  await watchHere.click()
  await otherWatchHere.waitFor()
  await page.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  await wait(() => clients === 1, 'Watch here must leave a single stream')
  // The separate camera window is a CC2 Control page under the same rule, not the raw stream.
  const [popup] = await Promise.all([
    context.waitForEvent('page'),
    page.getByRole('button', { name: 'Open in new window', exact: true }).click(),
  ])
  popup.on('pageerror', e => errors.push(e.message))
  await popup.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  assert.ok(new URL(popup.url()).hash === '#camera', 'the camera window opens the #camera view')
  await start.waitFor()
  await wait(() => clients === 1, 'only the camera window streams')
  await otherWatchHere.click()
  await popup.getByRole('button', { name: 'Watch here', exact: true }).waitFor()
  await wait(() => clients === 1, 'the camera window must hand the stream over')
  await popup.close()
  await other.close()
  await wait(() => clients === 0, 'closing the pages must close the stream')
  // A failed or mismatched claim must not bypass ownership and open another stream.
  for (const response of [
    { status: 503, json: { error: 'Claim unavailable' } },
    { status: 200, json: { viewer: 'another-viewer' } },
  ]) {
    await context.route('**/api/camera/claim', r => r.fulfill(response))
    const before = requests
    await start.click()
    await start.waitFor({ state: 'visible' })
    await page.waitForTimeout(250)
    assert.equal(clients, 0, 'an unconfirmed claim must not open a stream')
    assert.equal(requests, before, 'failed ownership must not request the camera')
    await context.unroute('**/api/camera/claim')
  }
  reject = true
  await page.clock.install()
  await page.evaluate(() => {
    window.__cc2CameraErrors = 0
    document.addEventListener('error', e => {
      if (e.target instanceof HTMLImageElement && e.target.closest('.cc2-camera-frame'))
        window.__cc2CameraErrors++
    }, true)
  })
  await start.click()
  const baseline = requests
  await page.getByRole('button', { name: 'Retry now', exact: true }).waitFor()
  const retryNow = page.getByRole('button', { name: 'Retry now', exact: true })
  const firstError = await page.evaluate(() => window.__cc2CameraErrors)
  await retryNow.click()
  await wait(() => requests > baseline, 'manual retry must open a new request')
  await page.waitForFunction(n => window.__cc2CameraErrors > n, firstError)
  for (const delay of [2500, 5000, 10000, 20000, 30000]) {
    const before = requests
    const failures = await page.evaluate(() => window.__cc2CameraErrors)
    await retryNow.waitFor()
    await page.clock.runFor(delay)
    await wait(() => requests > before, 'automatic retry must fire')
    await page.waitForFunction(n => window.__cc2CameraErrors > n, failures)
  }
  await page.getByText('Camera stream unavailable. Press Retry to try again.', { exact: true }).waitFor()
  const exhausted = requests
  await page.clock.runFor(60000)
  assert.equal(requests, exhausted, 'retry budget must stop further requests')
  reject = false
  await retryNow.click()
  await wait(() => clients === 1, 'manual retry must recover after budget exhaustion')
  await pause.click()
  await wait(() => clients === 0, 'recovered stream must close')
  assert.deepEqual(errors, [])
  console.log('PASS: manual start, real MJPEG cancellation, navigation, offscreen/visibility pause, one viewer at a time with handover and camera window, bounded retries and recovery')
} finally {
  await browser?.close()
  await server.close()
  stream.closeAllConnections()
  await new Promise(r => stream.close(r))
}
