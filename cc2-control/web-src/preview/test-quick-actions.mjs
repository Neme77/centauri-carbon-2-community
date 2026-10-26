import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
let light = 0, status = 2
const commands = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark', quick_actions: ['light:toggle', 'system:heaters_off', 'page:control', 'page:files'] } }))
  await page.route('**/api/printer', r => r.fulfill({ json: {
    connected: true, last_message_age: 0,
    machine: { status, status_name: status === 1 ? 'Idle' : status === 3 ? 'Paused' : 'Printing', progress: 20 },
    print: { filename: status === 1 ? '' : 'fixture.gcode', state: status === 1 ? 'idle' : status === 3 ? 'paused' : 'printing' },
    hardware: { light },
  } }))
  await page.route('**/api/control', r => {
    const action = r.request().postData()
    commands.push(action)
    // Deliberately do not update telemetry: accepted commands are not readback.
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(server.resolvedUrls.local[0])
  const button = page.getByRole('button', { name: 'Lights', exact: true })
  await button.waitFor()
  const state = async (on) => {
    await page.waitForFunction(on => document.querySelector('button[aria-pressed]')?.getAttribute('aria-pressed') === String(on), on)
    assert.equal(await button.getAttribute('aria-pressed'), String(on))
    assert.ok(await button.locator('svg').evaluate((icon, on) => icon.classList.contains(on ? 'text-cyan' : 'text-muted'), on))
  }
  await state(false)
  assert.ok(await button.isEnabled(), 'Lights remain available during printing')
  assert.ok(await page.getByRole('button', { name: 'All Heaters Off', exact: true }).isDisabled())
  await button.click()
  assert.deepEqual(commands, ['light:on'])
  await state(false)
  light = 1
  await state(true)
  await button.click()
  assert.deepEqual(commands, ['light:on', 'light:off'])
  await state(true)
  light = 0
  await state(false)
  // An external light change is reflected without another command.
  light = 1
  await state(true)
  assert.equal(commands.length, 2)
  status = 3
  await page.locator('#cc2-navigation a[href="#control"]').click()
  assert.ok(await page.getByRole('button', { name: 'Heaters Off', exact: true }).isDisabled())
  status = 1
  await page.waitForFunction(() => [...document.querySelectorAll('button')].some(b => b.textContent.trim() === 'Heaters Off' && !b.disabled))
  assert.deepEqual(errors, [])
  console.log('PASS: quick light follows telemetry, clicks send correct commands, external changes update its icon, heater shutdown is blocked in printing/paused states')
} finally {
  await browser?.close()
  await server.close()
}
