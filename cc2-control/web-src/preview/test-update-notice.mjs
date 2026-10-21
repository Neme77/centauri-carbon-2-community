import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir, readFile } from 'node:fs/promises'

const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  let installed = '1.2.14', checks = 0, verified = 0, packages = 0, stops = 0
  let offline = false, reject = false
  const index = { schema: 1, expires: Math.floor(Date.now() / 1000) + 86400, releases: [
    { version: '1.2.15', firmware: '4.4', channel: 'stable', url: 'https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.15-V4.4/Control.cc2update', size: 1500000, notes: 'Stable fixture' },
    { version: '9.0.0', firmware: '4.3', channel: 'stable', url: 'https://github.com/Neme77/centauri-carbon-2-community/releases/download/test/Control.cc2update', size: 1500000, notes: 'Wrong firmware' },
    { version: '1.3.0', firmware: '4.4', channel: 'canvas-uds', url: 'https://github.com/Neme77/centauri-carbon-2-community/releases/download/test/Control.cc2update', size: 1500000, notes: 'Experimental fixture' },
  ] }
  let cache = { schema: 1, expires: 0, releases: [] }
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/health', r => r.fulfill({ json: { version: installed, mqtt_connected: true, mqtt_registered: true, snapshot_received: true } }))
  await page.route('**/api/app-update/status', r => r.fulfill({ json: { enabled: true, ready: true, busy: false, installed, firmware: '4.4', phase: 'idle', job: '', version: '' } }))
  await page.route('https://raw.githubusercontent.com/**/update-channel.cc2catalog', async r => {
    checks++
    if (offline) return r.abort('failed')
    await new Promise(resolve => setTimeout(resolve, 150))
    return r.fulfill({ body: 'signed-catalog-fixture', contentType: 'application/octet-stream', headers: { 'Access-Control-Allow-Origin': '*' } })
  })
  await page.route('**/api/app-update/catalog', r => {
    if (r.request().method() === 'POST') {
      verified++
      assert.equal(r.request().headers()['x-cc2-request'], '1')
      assert.equal(r.request().headers()['x-cc2-lan'], undefined)
      if (reject) return r.fulfill({ status: 400, json: { error: 'Signature rejected' } })
      cache = JSON.parse(JSON.stringify(index))
    }
    return r.fulfill({ json: cache })
  })
  page.on('request', r => { if (/\/api\/app-update\/(download|upload|install)$/.test(r.url())) packages++ })
  await page.route('**/api/control', r => {
    if (r.request().postData() === 'system:emergency_stop') stops++
    return r.fulfill({ json: { accepted: true } })
  })
  await page.goto(server.resolvedUrls.local[0] + '#dashboard')
  const bell = page.locator('[data-update-notice]')
  const dot = page.locator('[data-update-dot]')
  await page.getByRole('button', { name: 'CC2 Control 1.2.15 available', exact: true }).waitFor()
  assert.equal(await dot.count(), 1)
  assert.equal(checks, 1)
  assert.equal(verified, 1)
  assert.equal(packages, 0)
  await bell.click()
  await page.getByRole('dialog').getByText('CC2 Control 1.2.15 available', { exact: true }).waitFor()
  await page.waitForFunction(() => document.querySelector('[role=dialog]')?.contains(document.activeElement))
  await page.keyboard.press('Escape')
  await page.getByRole('dialog').waitFor({ state: 'hidden' })
  assert.equal(await page.getByRole('dialog').count(), 0)
  assert.equal(await bell.evaluate(el => el === document.activeElement), true)
  await page.goto(server.resolvedUrls.local[0] + '#files')
  assert.equal(await dot.count(), 1)
  await bell.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Open updates', exact: true }).click()
  await page.getByRole('tab', { name: 'Updates', exact: true }).waitFor()
  assert.equal(await page.getByRole('tab', { name: 'Updates', exact: true }).getAttribute('aria-selected'), 'true')
  await page.getByText('Compatible update available: 1.2.15', { exact: true }).waitFor()
  assert.equal(checks, 1, 'header and Settings must share the verified cache')
  await page.getByRole('tab', { name: 'Appearance', exact: true }).click()
  await bell.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Open updates', exact: true }).click()
  await page.getByText('Compatible update available: 1.2.15', { exact: true }).waitFor()
  await page.getByLabel('Update channel', { exact: true }).selectOption('canvas-uds')
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByRole('button', { name: 'CC2 Control 1.3.0 available', exact: true }).waitFor()
  await page.getByLabel('Update channel', { exact: true }).selectOption('stable')

  const shots = process.env.CC2_NOTICE_SCREENSHOTS
  if (shots) await mkdir(shots, { recursive: true })
  for (const width of [1920, 1440, 1024, 768, 390, 320]) {
    await page.setViewportSize({ width, height: 1000 })
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'page overflow ' + width)
    const bounds = await page.locator('.cc2-header-actions').boundingBox()
    assert(bounds.x + bounds.width <= width, 'header actions overflow ' + width + ' ' + JSON.stringify(await page.locator('.cc2-topbar').evaluate(el => [...el.children].map(c=>({text:c.textContent,box:c.getBoundingClientRect().toJSON()})))))
    const b = await bell.boundingBox()
    assert(b.width >= 44 && (width > 900 || b.height >= 44), 'bell target ' + width)
    const stop = await page.locator('.cc2-emergency-stop').boundingBox()
    assert(stop.x >= b.x + b.width, 'bell overlaps emergency stop ' + width)
    await bell.click()
    await page.getByRole('dialog').waitFor()
    const backdrop = await page.getByRole('dialog').evaluate(el => el.parentElement.getBoundingClientRect().toJSON())
    assert(Math.abs(backdrop.height - 1000) < 1, 'notification backdrop must cover the viewport, not a 36px header strip')
    assert(Math.abs(backdrop.width - width) < 1, 'notification backdrop must span the viewport')
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'notice overflow ' + width)
    if (shots && [1440, 390].includes(width)) await page.screenshot({ path: path.join(shots, 'notice-' + width + '.png') })
    await page.waitForFunction(() => document.querySelector('[role=dialog]')?.contains(document.activeElement))
  await page.keyboard.press('Escape')
  await page.getByRole('dialog').waitFor({ state: 'hidden' })
  }
  await page.setViewportSize({ width: 1440, height: 1000 })
  const stop = page.getByRole('button', { name: 'Hold for emergency stop', exact: true })
  await stop.click()
  assert.equal(stops, 0, 'plain click must not trigger emergency stop')
  await stop.focus()
  await page.keyboard.down('Enter')
  await page.waitForTimeout(1100)
  await page.keyboard.up('Enter')
  assert.equal(stops, 1, 'one-second hold must still trigger exactly once')

  installed = '1.2.15'
  await page.waitForFunction(() => !document.querySelector('[data-update-dot]'))
  assert.equal(await bell.getAttribute('aria-label'), 'Update notifications')
  index.releases[0].version = '1.2.13'
  await page.getByRole('button', { name: 'Check for updates', exact: true }).click()
  await page.getByText('No newer compatible update published.', { exact: true }).waitFor()
  assert.equal(await dot.count(), 0, 'older release must not notify')
  assert.equal(packages, 0, 'notification interactions never download or install')

  // A fresh application must fail silently when GitHub or the verifier refuses the catalog.
  installed = '1.2.14'
  index.releases[0].version = '1.2.15'
  for (const mode of ['offline', 'signature']) {
    offline = mode === 'offline'
    reject = mode === 'signature'
    cache = { schema: 1, expires: 0, releases: [] }
    await page.evaluate(() => localStorage.removeItem('cc2-update-checked'))
    const before = checks
    await page.goto(server.resolvedUrls.local[0] + '#dashboard')
    await page.reload()
    await page.waitForFunction(() => document.querySelector('[data-update-notice]'))
    await page.waitForTimeout(500)
    assert.equal(checks, before + 1)
    assert.equal(await dot.count(), 0)
    assert.equal(await page.getByRole('alert').count(), 0, 'automatic errors stay silent')
    await page.goto(server.resolvedUrls.local[0] + '#files')
    await page.waitForTimeout(200)
    assert.equal(checks, before + 1, 'navigation must not bypass retry backoff')
  }
  assert.equal(packages, 0)
  assert.deepEqual(errors, [])
  const dict = JSON.parse(await readFile(path.join(root, 'public/locales/it.json')))
  assert.match(dict['updates.notice_available'], /\{version\}/)
  console.log('PASS: automatic verified notification, newer/firmware/channel filters, shared cache, direct Settings navigation, desktop/mobile geometry, emergency hold, silent offline/signature failure and no package actions')
} finally { await browser?.close(); await server.close() }
