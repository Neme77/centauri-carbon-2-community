import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
const server = await createServer({ root: new URL('..', import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser
const session = {
  enabled: true, active: false, state: 'off', reason: 'none', fresh: true, bed: 25, target: 0, temperature: 0, minutes: 0, remaining: 0,
  mode: 'bed', chamber_target: 0, chamber: 31.5, ready: false, levelled: false, plateau: null, fan: 0, bed_limit: 100,
}
const actions = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async r => {
    const response = await r.fetch(); const data = await response.json()
    data.connected = true; data.machine.status = 1; data.extruder = { temperature: 25, target: 0 }; data.heater_bed = { temperature: session.bed, target: session.target }
    data.bed_drying = { ...session }; data.experiments = { bed_drying: true }
    return r.fulfill({ json: data })
  })
  await page.route('**/api/bed-drying/*', r => {
    assert.equal(r.request().headers()['x-cc2-lan'], 'fixture-confirmation')
    actions.push([r.request().url().split('/').pop(), r.request().postData()])
    if (actions.at(-1)[0] === 'preheat') Object.assign(session, { active: true, state: 'heating', mode: 'preheat', target: 100, temperature: 100, minutes: 30, chamber_target: 45, fan: 51 })
    else Object.assign(session, { state: 'stopping', reason: 'manual' })
    return r.fulfill({ json: session })
  })
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  const card = page.locator('[data-bed-drying]'); await card.waitFor()
  // Drying stays the default; the switch shows the preheat fields and limits.
  await card.getByRole('button', { name: 'Start bed heating', exact: true }).waitFor()
  await card.getByRole('button', { name: 'Chamber preheat', exact: true }).click()
  await card.getByText('Chamber preheat', { exact: true }).first().waitFor()
  assert.equal(await card.getByRole('button', { name: 'Chamber preheat', exact: true }).getAttribute('aria-pressed'), 'true')
  await card.locator('[data-preheat-ambient]').waitFor()
  const start = card.getByRole('button', { name: 'Start chamber preheat', exact: true })
  await card.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  assert(await start.isEnabled())
  await card.getByLabel('Chamber target, °C', { exact: true }).fill('60'); assert(await start.isDisabled(), 'Above the bed-only limit')
  await card.getByLabel('Chamber target, °C', { exact: true }).fill('45')
  await card.getByLabel('Bed temperature, °C', { exact: true }).fill('101'); assert(await start.isDisabled(), 'Above the bed limit')
  await card.getByLabel('Bed temperature, °C', { exact: true }).fill('100')
  await card.getByLabel('Hold once ready, minutes', { exact: true }).fill('30')
  await start.click()
  const dialog = page.getByRole('dialog'); await dialog.getByText(/100 °C/).waitFor()
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click(); assert.deepEqual(actions, [])
  await start.click(); await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await card.getByText('Heating', { exact: true }).waitFor(); assert.deepEqual(actions, [['preheat', '100:45:30']])
  assert(await start.isDisabled())
  await card.locator('[data-drying-readback]').getByText(/Chamber 31\.5 \/ 45 °C/).waitFor()
  await card.locator('[data-drying-readback]').getByText(/Aux fan 20 %/).waitFor()
  // A chamber levelling off short of the target is called out.
  Object.assign(session, { chamber: 39.4, fan: 51, plateau: 40.2, bed: 99.8 })
  await card.locator('[data-preheat-short]').getByText(/about 40 °C, short of the 45 °C target/).waitFor()
  // Ready: a status line on the card and a popup on every page until dismissed.
  Object.assign(session, { state: 'holding', ready: true, chamber: 45.1, plateau: 45.3, fan: 0, remaining: 1700 })
  await card.locator('[data-preheat-ready]').waitFor()
  assert.equal(await card.locator('[data-preheat-short]').count(), 0)
  const popup = page.locator('[data-chamber-ready]'); await popup.getByText(/45\.1 °C \(target 45 °C\)/).waitFor()
  await page.goto(`${server.resolvedUrls.local[0]}#dashboard`); await popup.waitFor()
  await mkdir('preview/artifacts', { recursive: true }); await page.screenshot({ path: 'preview/artifacts/chamber-ready-popup.png' })
  await popup.getByRole('button', { name: 'Confirm', exact: true }).click(); assert.equal(await popup.count(), 0)
  await page.goto(`${server.resolvedUrls.local[0]}#control`); await card.waitFor()
  assert.equal(await popup.count(), 0, 'A dismissed popup stays closed for this session')
  // Levelled off short of the target: the hold says so instead of claiming the target.
  Object.assign(session, { state: 'heating', ready: false }); await card.getByText('Heating', { exact: true }).waitFor()
  Object.assign(session, { state: 'holding', ready: true, levelled: true, chamber: 43, plateau: 43 })
  await card.locator('[data-preheat-ready]').getByText(/levelled off short of the target/).waitFor()
  await popup.getByText(/levelled off at 43\.0 °C, short of the 45 °C target/).waitFor()
  await popup.getByRole('button', { name: 'Confirm', exact: true }).click()
  Object.assign(session, { levelled: false, chamber: 45.1 })
  await card.screenshot({ path: 'preview/artifacts/chamber-preheat-desktop.png' })
  // A print taking over and then finishing.
  Object.assign(session, { state: 'handed_off', reason: 'print', ready: true })
  await card.getByText('Print started · preheat handed over', { exact: true }).first().waitFor()
  Object.assign(session, { active: false, state: 'completed', reason: 'print' })
  await page.reload(); await card.getByText(/A print took over the bed/).waitFor()
  assert.equal(await card.getByRole('button', { name: 'Chamber preheat', exact: true }).getAttribute('aria-pressed'), 'true', 'A finished preheat reopens in preheat mode')
  Object.assign(session, { state: 'interrupted', reason: 'heat_timeout' })
  await card.getByText(/Stopped after 2 hours/).waitFor()
  await page.setViewportSize({ width: 390, height: 844 }); await card.scrollIntoViewIfNeeded()
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1))
  await card.screenshot({ path: 'preview/artifacts/chamber-preheat-mobile.png' })
  assert.deepEqual(errors, []); assert.equal(actions.length, 1)
  console.log('PASS chamber preheat: mode switch, limits, confirmation, live chamber/fan readback, levelling-off warning, ready status and global popup, hand-off and end messages, mobile layout')
} finally { await browser?.close(); await server.close() }
