import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir } from 'node:fs/promises'

const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
const key = 'cc2-remembered-lan-code', secret = 'fixture-personal-code'
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } })
  const errors = [], actions = [], checks = []
  let reject = false
  const state = { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: true, quiet_motors: false, normal_confirmed: true, cli_canvas: false, bed_drying: false }
  await context.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await context.route('**/api/app-update/status', r => r.fulfill({ json: { enabled: true, ready: true, busy: false, installed: '1.2.21', firmware: '4.4', phase: 'ready', job: 'a'.repeat(32) } }))
  await context.route('**/api/access/verify', r => {
    checks.push(r.request().headers()['x-cc2-lan'])
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    return r.fulfill({ status: checks.at(-1) === secret ? 200 : 401, json: checks.at(-1) === secret ? { verified: true } : { error: 'LAN code rejected' } })
  })
  await context.route('**/api/experiments', r => {
    if (r.request().method() === 'POST') {
      assert.equal(r.request().headers()['x-cc2-lan'], secret)
      actions.push(r.request().postData())
      if (reject) return r.fulfill({ status: 401, json: { error: 'LAN code rejected' } })
      state.canvas_uds = !state.canvas_uds
    }
    return r.fulfill({ json: state })
  })
  await context.route('**/api/app-update/install', r => {
    assert.equal(r.request().headers()['x-cc2-lan'], secret)
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    actions.push('install')
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  context.on('request', req => {
    if (req.headers()['x-cc2-lan']) assert(['/api/access/verify', '/api/experiments', '/api/app-update/install'].includes(new URL(req.url()).pathname), 'Never attach credentials to polling, catalog, diagnostics or external requests')
  })
  const base = server.resolvedUrls.local[0]
  const go = hash => page.goto(base + hash)
  const toggle = page.getByRole('switch', { name: 'Remember the LAN code on this device', exact: true })
  const saved = () => page.evaluate(k => localStorage.getItem(k), key)
  const enable = async () => {
    await toggle.click()
    await page.locator('#remember-lan-code').fill(secret)
    await page.getByRole('button', { name: 'Verify and remember', exact: true }).click()
    await page.getByRole('dialog').waitFor({ state: 'hidden' })
    assert.equal(await saved(), secret)
  }
  const confirm = () => page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await go('#settings/connection')
  await toggle.waitFor()
  assert.equal(await toggle.getAttribute('aria-checked'), 'false')
  await toggle.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(await saved(), null); assert.deepEqual(checks, [])
  await toggle.click()
  await page.locator('#remember-lan-code').fill('wrong')
  await page.getByRole('button', { name: 'Verify and remember', exact: true }).click()
  await page.getByRole('alert').filter({ hasText: 'LAN code rejected' }).waitFor()
  assert.equal(await saved(), null)
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  await enable()
  console.log('PASS opt-in, cancellation, wrong-code refusal and verified save')
  await page.reload(); await toggle.waitFor(); assert.equal(await toggle.getAttribute('aria-checked'), 'true')
  await go('#settings/updates')
  const updateCode = page.locator('#update-lan-code')
  await updateCode.waitFor(); assert.equal(await updateCode.inputValue(), secret)
  assert.equal(await updateCode.getAttribute('type'), 'password')
  await page.getByRole('button', { name: 'Install verified update', exact: true }).click()
  assert.deepEqual(actions, [], 'Stored code must not bypass confirmation')
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.getByRole('button', { name: 'Install verified update', exact: true }).click(); await confirm()
  await page.getByText('CC2 Control is restarting', { exact: false }).waitFor()
  assert.deepEqual(actions, ['install']); assert.equal(await updateCode.inputValue(), secret)
  await go('#settings/integrations')
  const experimentCode = page.locator('#experiments-lan')
  await experimentCode.waitFor(); assert.equal(await experimentCode.inputValue(), secret)
  const canvas = page.getByRole('switch', { name: 'Canvas UDS', exact: true })
  await canvas.click(); assert.deepEqual(actions, ['install']); await confirm()
  await page.waitForFunction(() => document.querySelector('[data-experiments] [role=switch]').getAttribute('aria-checked') === 'true')
  assert.equal(await saved(), secret); assert.equal(await experimentCode.inputValue(), secret)
  reject = true
  await canvas.click(); await confirm()
  await page.getByRole('alert').filter({ hasText: 'LAN code rejected' }).waitFor()
  assert.equal(await saved(), null); assert.equal(await experimentCode.inputValue(), '')
  assert(await canvas.isDisabled())
  reject = false
  await go('#settings/connection'); await enable()
  const second = await context.newPage(); await second.goto(base + '#settings/updates')
  await second.locator('#update-lan-code').waitFor(); assert.equal(await second.locator('#update-lan-code').inputValue(), secret)
  await toggle.click()
  await second.waitForFunction(() => document.querySelector('#update-lan-code').value === '')
  assert.equal(await saved(), null)
  await enable()
  await page.getByRole('button', { name: 'Forget code', exact: true }).click()
  assert.equal(await saved(), null)
  await enable()
  // Another origin and another browser profile never inherit the saved secret.
  const other = await context.newPage(); await other.goto(base.replace('127.0.0.1', 'localhost') + '#settings/updates')
  await other.locator('#update-lan-code').waitFor(); assert.equal(await other.locator('#update-lan-code').inputValue(), '')
  const privateContext = await browser.newContext(); const fresh = await privateContext.newPage()
  await fresh.goto(base + '#settings/updates'); await fresh.locator('#update-lan-code').waitFor()
  assert.equal(await fresh.locator('#update-lan-code').inputValue(), '')
  await privateContext.close()
  await page.setViewportSize({ width: 390, height: 844 }); await toggle.click(); await toggle.click()
  await page.getByRole('dialog').waitFor()
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))
  await mkdir(path.join(root, 'preview/screenshots'), { recursive: true })
  await page.screenshot({ path: path.join(root, 'preview/screenshots/remember-lan-mobile.png') })
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  // Storage blocked: do not claim success or send any printer command.
  const blockedContext = await browser.newContext()
  await blockedContext.addInitScript(() => { Storage.prototype.setItem = () => { throw new DOMException('blocked', 'SecurityError') } })
  await blockedContext.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await blockedContext.route('**/api/access/verify', r => r.fulfill({ json: { verified: true } }))
  const blocked = await blockedContext.newPage(); await blocked.goto(base + '#settings/connection')
  await blocked.getByRole('switch', { name: 'Remember the LAN code on this device', exact: true }).click()
  await blocked.locator('#remember-lan-code').fill(secret)
  await blocked.getByRole('button', { name: 'Verify and remember', exact: true }).click()
  await blocked.getByRole('alert').filter({ hasText: 'The code was not saved' }).waitFor()
  assert.equal(await blocked.evaluate(k => localStorage.getItem(k), key), null)
  await blockedContext.close()
  assert.deepEqual(errors, [])
  console.log('PASS: optional verified LAN remembering, masked update/experiment reuse with confirmations, reload, refusal, forgetting, tab/profile/origin isolation, mobile and blocked storage')
} finally { await browser?.close(); await server.close() }
