import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Exercise the real language selector, persisted reload and newly translated
// queue, feed and RFID controls. Every printer response is a local fixture.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true })
  const origin = server.resolvedUrls.local[0]
  for (const code of ['cs', 'de', 'es', 'fr', 'hu', 'it', 'pl', 'ru', 'tr', 'zh']) {
    const labels = JSON.parse(await readFile(new URL(`../public/locales/${code}.json`, import.meta.url), 'utf8'))
    const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } })
    const saved = await context.request.put(origin + 'api/preferences', {
      headers: { 'X-CC2-Request': '1' }, data: { language: code },
    })
    assert.equal(saved.status(), 200, `${code}: demo accepts shipped locale`)
    assert.equal((await context.request.get(origin + 'i18n/xx.json')).status(), 404)
    assert.equal((await context.request.put(origin + 'api/preferences', {
      headers: { 'X-CC2-Request': '1' }, data: { language: 'xx' },
    })).status(), 400, 'demo rejects unknown locale')
    const page = await context.newPage()
    const errors = [], mutations = []
    let language = 'en', phase = 5, scanning = false
    page.on('pageerror', error => errors.push(error.message))
    console.log(`CHECK ${code}: selecting language`)
    await page.route('**/api/preferences', async route => {
      if (route.request().method() !== 'GET') language = route.request().postDataJSON().language || language
      await route.fulfill({ json: { language, theme: 'light' } })
    })
    await page.route('**/api/printer', async route => {
      const response = await route.fetch(), data = await response.json()
      Object.assign(data, { connected: true, last_message_age: 0 })
      Object.assign(data.machine, { status: 1, status_name: 'Idle' })
      Object.assign(data.print, { state: 'complete', filename: '' })
      await route.fulfill({ json: data })
    })
    await page.route('**/api/queue', route => route.fulfill({ json: {
      available: true, error: '', revision: 1, printer_idle: true,
      policy: { after: 'wait', delay: 60, remove_done: false },
      active: null, next: null, auto_in: null, items: [],
    } }))
    await page.route('**/api/external-filament', route => route.fulfill({ json: {
      available: true, experiment: true, source: 'canvas', can_select: true,
      idle: false, can_unload: false, filament: { manufacturer: 'Generic', type: 'PLA', color: '0xF72221' },
    } }))
    await page.route('**/api/rfid/lifecycle', route => route.fulfill({ json: {
      installed: true, metadata_valid: true, phase, busy: false,
      reboot_required: false, recovery_required: false, retry_required: false, activation_requested: false,
    } }))
    await page.route('**/api/rfid', route => route.fulfill({ json: {
      installed: true, ready: true, busy: scanning, armed: scanning,
      state: scanning ? 'scanning' : 'idle', generation: 1, preview: null,
    } }))
    await page.route('**/api/rfid/action', route => {
      mutations.push(route.request().postData())
      scanning = route.request().postData() === 'scan'
      return route.fulfill({ json: { accepted: true } })
    })
    await page.goto(origin + '#settings')
    await page.getByRole('tab').nth(2).click()
    await Promise.all([
      page.waitForResponse(response => response.url().endsWith('/api/preferences') && response.request().method() === 'PUT'),
      page.locator('main select:has(option[value="pl"])').selectOption(code),
    ]).catch(async error => {
      console.error(await page.locator('body').innerText())
      throw error
    })
    await page.waitForFunction(lang => document.documentElement.lang === lang, code)
    assert.equal(language, code, 'language preference persisted')
    await page.reload()
    await page.waitForFunction(lang => document.documentElement.lang === lang, code)
    await page.goto(origin + '#queue')
    await page.getByRole('heading', { name: labels['queue.title'], exact: true }).waitFor()
    await page.getByText(labels['queue.empty_hint'], { exact: true }).waitFor()
    await page.goto(origin + '#canvas')
    await page.getByText(labels['feed.selection_hint'], { exact: true }).waitFor()
    await page.locator('[data-filament-source]').getByRole('button', { name: labels['filament.external'], exact: true }).click()
    await page.getByRole('dialog').getByText(labels['feed.select_confirm'], { exact: true }).waitFor()
    await page.getByRole('dialog').getByRole('button', { name: labels['common.cancel'], exact: true }).click()
    await page.locator('[data-rfid-toggle]').click()
    assert.equal((await page.locator('[data-rfid-start]').textContent()).trim(), labels['rfid.scan'])
    await page.locator('[data-rfid-start]').click()
    await page.getByText(labels['rfid.armed'], { exact: true }).waitFor()
    assert.deepEqual(mutations, ['scan'])
    for (const width of [1440, 390]) {
      await page.setViewportSize({ width, height: 1000 })
      await page.waitForTimeout(100)
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${code}: horizontal overflow`)
    }
    scanning = false
    phase = -1
    await page.goto(origin + '#settings')
    await page.getByRole('tab').nth(1).click()
    await page.getByText(labels['rfid.lifecycle_dormant'], { exact: true }).waitFor()
    assert.deepEqual(errors, [], `${code}: browser errors`)
    await context.close()
    console.log(`PASS ${code}: language selection/reload, queue, feed confirmation, RFID scan and dormant state, mobile width`)
  }
} finally {
  await browser?.close()
  await server.close()
}
