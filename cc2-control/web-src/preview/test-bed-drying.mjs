import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
const server = await createServer({ root: new URL('..', import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser
const choices = { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, bed_drying: false }
const session = { enabled: false, active: false, state: 'off', reason: 'none', fresh: true, bed: 25, target: 0, temperature: 70, minutes: 1, remaining: 0 }
const actions = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/experiments', r => {
    if (r.request().method() === 'POST') {
      assert.equal(r.request().headers()['x-cc2-lan'], 'fixture-confirmation')
      choices.bed_drying = r.request().postData() === 'bed-drying:on'
      session.enabled = choices.bed_drying
    }
    return r.fulfill({ json: choices })
  })
  await page.route('**/api/printer', async r => {
    const response = await r.fetch(); const data = await response.json()
    data.connected = true; data.machine.status = 1; data.extruder = { temperature: 25, target: 0 }; data.heater_bed = { temperature: session.bed, target: session.target };
    data.bed_drying = { ...session }; data.experiments = { bed_drying: choices.bed_drying }
    return r.fulfill({ json: data })
  })
  await page.route('**/api/bed-drying/*', r => {
    assert.equal(r.request().headers()['x-cc2-lan'], 'fixture-confirmation')
    actions.push([r.request().url().split('/').pop(), r.request().postData()])
    if (actions.at(-1)[0] === 'start') Object.assign(session, { active: true, state: 'heating', target: 70 })
    else Object.assign(session, { state: 'stopping' })
    return r.fulfill({ json: session })
  })
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  assert.equal(await page.locator('[data-bed-drying]').count(), 0)
  await page.goto(`${server.resolvedUrls.local[0]}#settings/integrations`)
  const toggle = page.getByRole('switch', { name: 'Bed heating for filament drying', exact: true })
  await toggle.waitFor(); assert(await toggle.isDisabled())
  await page.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  await toggle.click(); await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('[data-experiments] [role=switch]')[2]?.getAttribute('aria-checked') === 'true')
  assert.deepEqual(actions, [], 'Enabling controls must not heat')
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  const card = page.locator('[data-bed-drying]'); await card.waitFor()
  const start = card.getByRole('button', { name: 'Start bed heating', exact: true })
  assert(await start.isDisabled())
  await card.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  await card.getByLabel('Duration, minutes', { exact: true }).fill('0'); assert(await start.isDisabled())
  await card.getByLabel('Duration, minutes', { exact: true }).fill('1')
  await start.click(); await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click(); assert.deepEqual(actions, [])
  await start.click(); await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await card.getByText('Heating', { exact: true }).waitFor(); assert.deepEqual(actions, [['start', '70:1']])
  assert(await start.isDisabled()); assert(await card.getByLabel('Duration, minutes', { exact: true }).isDisabled())
  Object.assign(session, { state: 'holding', bed: 70, remaining: 55 })
  await card.getByText('Holding temperature', { exact: true }).waitFor()
  await page.reload(); await card.getByText('Holding temperature', { exact: true }).waitFor(); assert.equal(actions.length, 1)
  await card.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  await card.getByRole('button', { name: 'Stop bed heating', exact: true }).click()
  await card.getByText('Stopping · awaiting confirmation', { exact: true }).waitFor(); assert(session.active)
  Object.assign(session, { active: false, state: 'interrupted', target: 0, reason: 'manual' })
  await card.getByText('Interrupted', { exact: true }).waitFor()
  await page.setViewportSize({ width: 390, height: 844 }); await card.scrollIntoViewIfNeeded()
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1))
  await mkdir('preview/artifacts', { recursive: true })
  await card.screenshot({ path: 'preview/artifacts/bed-drying-mobile.png' })
  await page.setViewportSize({ width: 1440, height: 1000 }); await card.screenshot({ path: 'preview/artifacts/bed-drying-desktop.png' })
  assert.deepEqual(errors, []); assert.equal(actions.length, 2)
  console.log('PASS opt-in, no heat on enable, validation, actual start/cancel/stop controls, reload continuity, pending zero confirmation and mobile layout')
} finally { await browser?.close(); await server.close() }
