import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, machine = 2
let eject = { available: false, running: false, slot: -1, result: 'unavailable', travel: 0 }
const actions = [], errors = []
let cancels = 0
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1366, height: 950 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', r => r.fulfill({ json: { connected: true, last_message_age: 0, machine: { status: machine, status_name: machine === 1 ? 'Idle' : 'Printing' }, print: { state: machine === 1 ? 'idle' : 'printing' } } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { telemetry: { result: { canvas_info: { canvas_list: [{ connected: 1, tray_list: [] }] } } } } }))
  await page.route('**/api/canvas/eject', r => r.fulfill({ json: eject }))
  await page.route('**/api/control', r => { actions.push(r.request().postData()); eject = { available: true, running: true, slot: 2, result: 'running', travel: 15 }; return r.fulfill({ status: 202, json: { accepted: true } }) })
  await page.route('**/api/canvas/eject/cancel', r => { cancels++; eject = { ...eject, running: false, result: 'cancelled' }; return r.fulfill({ status: 202, json: { accepted: true } }) })
  await page.goto(`${server.resolvedUrls.local[0]}#canvas`)
  const card = page.locator('.cc2-canvas-eject')
  await card.waitFor(); assert.ok(await card.getByRole('button').isDisabled())
  machine = 1; eject = { ...eject, available: true, result: 'empty' }
  await page.waitForFunction(() => !document.querySelector('.cc2-canvas-eject button').disabled)
  await page.getByRole('button', { name: 'Select slot', exact: true }).nth(1).click()
  await card.getByRole('button').click()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click(); assert.equal(actions.length, 0)
  await card.getByRole('button').click()
  await page.getByRole('button', { name: 'Confirm', exact: true }).click()
  await card.getByRole('button', { name: 'Stop ejection', exact: true }).waitFor()
  assert.deepEqual(actions, ['canvas:eject:2'])
  assert.ok(await page.getByRole('button', { name: /Load.*Slot 3/ }).isDisabled())
  await card.getByRole('button', { name: 'Stop ejection', exact: true }).click()
  await page.waitForFunction(() => document.querySelector('.cc2-canvas-eject').textContent.includes('Ejection stopped'))
  assert.equal(cancels, 1)
  eject = { ...eject, result: 'travel_limit' }
  await page.waitForFunction(() => document.querySelector('.cc2-canvas-eject').textContent.includes('Travel limit'))
  await page.setViewportSize({ width: 390, height: 844 }); await card.scrollIntoViewIfNeeded()
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))
  assert.deepEqual(errors, [])
  console.log('PASS browser Canvas availability/idle guards, slot selection, confirmation, progress, cancellation, failure readback and mobile layout')
} finally { await browser?.close(); await server.close() }
