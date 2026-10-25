import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir } from 'node:fs/promises'

const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, unreachable = false
const state = { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false }
const choices = [], native = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.addInitScript(() => localStorage.setItem('cc2-update-channel', 'canvas-uds'))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/app-update/status', r => r.fulfill({ json: { enabled: false, ready: true, busy: false, installed: '1.2.19', firmware: '4.4' } }))
  await page.route('**/api/control', r => { native.push(r.request().postData()); return r.fulfill({ status: 409, json: { error: 'No native action expected' } }) })
  await page.route('**/api/experiments', r => {
    if (unreachable) return r.fulfill({ status: 503, json: { error: 'unavailable' } })
    if (r.request().method() === 'POST') {
      const action = r.request().postData()
      choices.push(action)
      if (r.request().headers()['x-cc2-lan'] !== 'fixture-confirmation') return r.fulfill({ status: 401, json: { error: 'LAN code rejected' } })
      if (!state.ready || !state.editable) return r.fulfill({ status: 409, json: { error: 'Printer is busy' } })
      if ((action === 'quiet-motors:off' || action.startsWith('canvas-uds:')) && state.quiet_motors && !state.normal_confirmed)
        return r.fulfill({ status: 409, json: { error: 'Return motors to Normal first' } })
      const [feature, enabled] = action.split(':')
      state[feature === 'canvas-uds' ? 'canvas_uds' : 'quiet_motors'] = enabled === 'on'
      if (feature === 'canvas-uds' && state.quiet_motors) state.normal_confirmed = false
    }
    return r.fulfill({ json: { ...state, ready: state.ready && (!state.quiet_motors || state.normal_confirmed) } })
  })
  await page.goto(`${server.resolvedUrls.local[0]}#settings/integrations`)
  const canvas = page.getByRole('switch', { name: 'Canvas UDS', exact: true })
  const quiet = page.getByRole('switch', { name: 'Quiet motor controls', exact: true })
  const code = page.getByLabel('LAN code for experimental features', { exact: true })
  const confirm = () => page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await canvas.waitFor(); assert(await canvas.isDisabled()); assert(await quiet.isDisabled())
  assert.equal(await page.getByLabel('Update channel', { exact: true }).count(), 0)
  await code.fill('fixture-confirmation')
  await canvas.click(); await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.deepEqual(choices, [])
  await canvas.click(); await confirm()
  await page.waitForFunction(() => document.querySelector('[data-experiments] [role=switch]')?.getAttribute('aria-checked') === 'true')
  await page.getByText('Enabled · using MQTT fallback', { exact: true }).waitFor()
  assert.deepEqual(choices, ['canvas-uds:on']); assert.deepEqual(native, [])
  assert.equal(await code.inputValue(), '')
  await page.reload(); await canvas.waitFor(); assert.equal(await canvas.getAttribute('aria-checked'), 'true')
  await code.fill('wrong')
  await quiet.click(); await confirm(); await page.getByRole('alert').filter({ hasText: 'LAN code rejected' }).waitFor()
  assert.equal(await quiet.getAttribute('aria-checked'), 'false')
  await code.fill('fixture-confirmation'); await quiet.click(); await confirm()
  await page.getByText('Normal needs confirmation.', { exact: false }).waitFor()
  assert.equal(await quiet.getAttribute('aria-checked'), 'true'); assert(await quiet.isDisabled())
  assert.equal(state.canvas_uds, true, 'Quiet opt-in must preserve Canvas')
  state.normal_confirmed = true
  await code.fill('fixture-confirmation'); await quiet.waitFor({ state: 'visible' })
  await page.waitForFunction(() => !document.querySelectorAll('[data-experiments] [role=switch]')[1].disabled)
  await quiet.click(); await confirm()
  await page.waitForFunction(() => document.querySelectorAll('[data-experiments] [role=switch]')[1].getAttribute('aria-checked') === 'false')
  assert(state.canvas_uds, 'Disabling Quiet must preserve Canvas')
  await code.fill('fixture-confirmation'); await canvas.click(); await confirm()
  await page.getByText('Disabled · using MQTT', { exact: true }).waitFor()
  assert.deepEqual(native, [], 'Preferences must never issue native commands')
  // Real printer case: changing the Canvas subscription revokes a prior Normal confirmation.
  state.canvas_uds = true; state.quiet_motors = true; state.normal_confirmed = true
  await code.fill('fixture-confirmation')
  await page.waitForFunction(() => document.querySelector('[data-experiments] [role=switch]')?.getAttribute('aria-checked') === 'true')
  await canvas.click(); await confirm()
  const recovery = page.locator('[data-experiment-recovery]')
  await recovery.waitFor()
  assert.equal(state.canvas_uds, false); assert(state.quiet_motors)
  assert(await canvas.isDisabled()); assert(await quiet.isDisabled())
  await recovery.getByRole('button', { name: 'Control', exact: true }).click()
  await page.waitForURL('**/#control')
  assert.deepEqual(native, [], 'Recovery link must navigate only, never send Normal automatically')
  await page.goto(`${server.resolvedUrls.local[0]}#settings/integrations`)
  await recovery.waitFor()
  state.normal_confirmed = true // The separately confirmed Normal command completes on the fresh connection.
  await recovery.waitFor({ state: 'hidden' })
  await code.fill('fixture-confirmation')
  await page.waitForFunction(() => !document.querySelectorAll('[data-experiments] [role=switch]')[1].disabled)
  await quiet.click(); await confirm()
  assert.equal(state.canvas_uds, false, 'Quiet opt-out must retain the selected Canvas source')
  assert.deepEqual(native, [])
  state.ready = false
  await code.fill('fixture-confirmation')
  await page.waitForFunction(() => [...document.querySelectorAll('[data-experiments] [role=switch]')].every(b => b.disabled))
  const count = choices.length
  state.ready = true; unreachable = true
  await page.getByText('Experimental settings are unavailable. No changes can be made.', { exact: true }).first().waitFor()
  assert(await canvas.isDisabled()); assert.equal(choices.length, count)
  unreachable = false
  for (const theme of ['dark', 'light']) for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1100 })
    await page.evaluate(t => { document.documentElement.dataset.theme = t }, theme)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${theme} overflow ${width}`)
  }
  await mkdir(path.join(root, '../build/preview-experiments'), { recursive: true })
  await page.screenshot({ path: path.join(root, '../build/preview-experiments/mobile.png'), fullPage: true })
  assert.deepEqual(errors, [])
  console.log('PASS independent opt-ins, confirmations, auth, backend truth, persistence, unavailable guards, no native commands and responsive layout')
} finally { await browser?.close(); await server.close() }
