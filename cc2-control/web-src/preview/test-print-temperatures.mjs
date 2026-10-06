import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, state = 'printing'
const actions = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : {}) })
  const page = await browser.newPage()
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', r => r.fulfill({ json: { connected: true, last_message_age: 0, machine: { status: state === 'loading' ? 3 : 2, status_name: state === 'loading' ? 'Loading' : 'Printing' }, print: { state, filename: 'large.gcode' }, extruder: { temperature: 200, target: 205 }, heater_bed: { temperature: 60, target: 60 } } }))
  await page.route('**/api/control', r => { actions.push(r.request().postData()); return r.fulfill({ status: 202, json: { accepted: true } }) })
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  const nozzle = page.getByRole('spinbutton', { name: 'Nozzle target', exact: true })
  const bed = page.getByRole('spinbutton', { name: 'Heated bed target', exact: true })
  const apply = page.getByRole('button', { name: 'Apply targets', exact: true })
  await nozzle.waitFor()
  await page.waitForFunction(() => document.querySelector('input[aria-label="Nozzle target"]')?.value === '205')
  assert.ok(await nozzle.isEnabled()); assert.ok(await bed.isEnabled())
  assert.ok(await page.getByRole('button', { name: 'PLA', exact: true }).first().isDisabled())
  await nozzle.fill('210'); await bed.fill('65')
  const firstReply = page.waitForResponse(r => r.url().endsWith('/api/control'))
  await apply.click(); await firstReply
  assert.deepEqual(actions, ['heaters:set:210:65'])
  await nozzle.fill('301'); await apply.click(); assert.equal(actions.length, 1)
  await nozzle.fill('215'); state = 'paused'
  await page.waitForTimeout(1500)
  const pausedReply = page.waitForResponse(r => r.url().endsWith('/api/control'))
  await apply.click(); await pausedReply
  assert.deepEqual(actions, ['heaters:set:210:65', 'heaters:set:215:65'])
  state = 'loading'
  await page.waitForFunction(() => document.querySelector('input[aria-label="Nozzle target"]')?.disabled)
  assert.ok(await apply.isDisabled()); assert.deepEqual(errors, [])
  console.log('PASS: seeded targets, editing while printing/paused, protected limits and other-state lock')
} finally { await browser?.close(); await server.close() }
