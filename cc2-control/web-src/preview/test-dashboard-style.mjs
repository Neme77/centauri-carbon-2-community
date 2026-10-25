import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], commands = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => { if (r.method() === 'POST' && r.url().endsWith('/api/control')) commands.push(r.postData()) })
  const origin = server.resolvedUrls.local[0]
  // The camera host can mount before the printer-backed Dashboard cards.
  // Exercise that loading order instead of relying on a fast local response.
  const printerDelay = Number(process.env.CC2_TEST_PRINTER_DELAY_MS || 200)
  await page.route('**/api/printer', async route => {
    await new Promise(resolve => setTimeout(resolve, printerDelay))
    await route.continue()
  })
  await page.goto(origin + '#dashboard')
  const slider = page.getByRole('slider', { name: 'Print speed (%)', exact: true })
  const speed = page.locator('#tune-speed')
  await slider.waitFor(); await page.waitForFunction(() => !document.querySelector('#tune-speed').disabled)
  await slider.focus(); await page.keyboard.press('ArrowRight')
  assert.equal(await speed.inputValue(), '101'); assert.deepEqual(commands, [])
  await speed.fill('125'); assert.equal(await slider.inputValue(), '125')
  await speed.press('Enter')
  await page.waitForFunction(() => document.querySelector('#tune-speed').getAttribute('placeholder') === '125')
  assert.deepEqual(commands, ['tune:speed:125'])
  await speed.locator('..').getByRole('button', { name: 'Reset 100%', exact: true }).click()
  await page.waitForFunction(() => document.querySelector('#tune-speed').getAttribute('placeholder') === '100')
  assert.deepEqual(commands, ['tune:speed:125', 'tune:speed:100'])
  assert.equal(await page.locator('.cc2-camera-paused p').count(), 0, 'ordinary idle camera shows only its start action')
  await page.getByRole('button', { name: 'Start live view', exact: true }).click()
  await page.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  const before = await page.locator('.cc2-camera-frame').boundingBox()
  await page.evaluate(() => { window.__cameraBefore = document.querySelector('.cc2-camera-frame img') })
  await page.getByRole('checkbox', { name: 'Keep active', exact: true }).check()
  assert.equal(await page.evaluate(() => document.querySelector('.cc2-camera-frame img') === window.__cameraBefore), true)
  const after = await page.locator('.cc2-camera-frame').boundingBox()
  assert.equal(before.height, after.height); assert.equal(before.width, after.width)
  await page.locator('.cc2-camera-card').getByRole('button', { name: 'Pause', exact: true }).click()
  for (const mode of ['light', 'dark']) for (const width of [1440, 390]) {
    await page.request.put(origin + 'api/preferences', { data: { language: 'en', theme: mode, camera_persistent: false } })
    await page.setViewportSize({ width, height: 1000 }); await page.reload()
    await page.getByRole('checkbox', { name: 'Keep active', exact: true }).waitFor()
    await page.locator('.cc2-quick-actions').waitFor({ state: 'visible' })
    await page.locator('.cc2-tuning').waitFor({ state: 'visible' })
    await page.waitForFunction(expected => document.documentElement.dataset.theme === expected, mode)
    const layout = await page.evaluate(() => {
      const r = q => document.querySelector(q).getBoundingClientRect()
      return { overflow: document.documentElement.scrollWidth > innerWidth, quick: r('.cc2-quick-actions').bottom, tuning: r('.cc2-tuning').bottom, camera: r('.cc2-camera-frame').height }
    })
    assert.equal(layout.overflow, false); assert(layout.camera >= 180)
    if (width === 1440) assert(Math.abs(layout.quick - layout.tuning) <= 1)
  }
  await page.route('**/__preview/camera.svg*', r => r.abort())
  await page.getByRole('button', { name: 'Start live view', exact: true }).click()
  await page.getByRole('button', { name: 'Retry now', exact: true }).waitFor({ state: 'visible' })
  const retryFits = await page.evaluate(() => {
    const frame = document.querySelector('.cc2-camera-frame').getBoundingClientRect()
    const button = document.querySelector('.cc2-camera-frame button').getBoundingClientRect()
    return button.top >= frame.top && button.bottom <= frame.bottom
  })
  assert(retryFits, 'mobile error feedback must keep the full retry button inside the camera frame')
  await page.request.post(origin + '__preview/scenario', { data: { scene: 'idle' } }); await page.reload()
  await page.waitForFunction(() => document.querySelector('#tune-speed')?.disabled === true)
  assert(await slider.isDisabled()); assert.deepEqual(commands, ['tune:speed:125', 'tune:speed:100'])
  await page.goto(origin + '#settings/appearance')
  await page.getByRole('heading', { name: 'Appearance', exact: true }).waitFor()
  assert.equal(await page.getByRole('checkbox', { name: 'Keep active', exact: true }).count(), 0)
  assert.deepEqual(errors, [])
  console.log('PASS: draft sliders, explicit Apply/Reset, idle guards, stable camera toggle/image, desktop/mobile theme alignment')
} finally {
  await browser?.close()
  await server.close()
}

