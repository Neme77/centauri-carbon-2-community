import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { chromium } from 'playwright'
import { createServer } from 'vite'
import { settledViewport } from './viewport.mjs'

const root = fileURLToPath(new URL('..', import.meta.url))
const labels = JSON.parse(await readFile(path.join(root, 'public/locales/en.json'), 'utf8'))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser, pressure = 0.04237, status = 1, state = 'standby', age = 0, connected = true, refuse = false
const actions = [], errors = []
const actual = value => labels['tuning.actual_pa'].replace('{value}', String(value))
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  page.on('pageerror', error => errors.push(error.message))
  await page.route('**/api/preferences', route => route.fulfill({ json: { language: 'en', theme: 'obsidian' } }))
  await page.route('**/api/printer', route => route.fulfill({ json: {
    connected, last_message_age: age, machine: { status, status_name: status === 1 ? 'Idle' : 'Printing', sub_status: 0 },
    print: { enabled: status === 2, state: status === 2 ? state : '', filename: status === 2 ? 'fixture.gcode' : '', native: { state } },
    extruder: { temperature: 25, target: 0 }, heater_bed: { temperature: 25, target: 0 },
    tuning: { speed_percent: 100, flow_percent: 100, live_velocity: 0, pressure_advance: pressure },
  } }))
  await page.route('**/api/control', route => {
    actions.push(route.request().postData())
    return route.fulfill(refuse ? { status: 409, json: { error: 'Fixture refusal' } } : { status: 202, json: { accepted: true } })
  })
  await page.goto(server.resolvedUrls.local[0] + '#control')
  const row = page.locator('.cc2-tuning-field').filter({ has: page.locator('#tune-pressure-advance') })
  const input = row.getByRole('spinbutton')
  const apply = row.getByRole('button', { name: labels['tuning.apply'], exact: true })
  await row.getByText(actual(0.04237), { exact: true }).waitFor()
  assert(await input.isEnabled(), 'Pressure Advance may be applied at native Idle without heating')
  assert(await page.locator('#tune-speed').isDisabled() && await page.locator('#tune-flow').isDisabled())
  for (const value of ['-0.1', '1.000001', '0.0000001']) {
    await input.fill(value); assert(await apply.isDisabled())
  }
  await input.fill('0.05'); assert.deepEqual(actions, [], 'Editing must not send commands')
  await apply.click()
  await page.waitForFunction(() => document.querySelector('#tune-pressure-advance')?.disabled)
  assert.deepEqual(actions, ['tune:pressure-advance:0.05'])
  await row.getByText(actual(0.04237), { exact: true }).waitFor()
  pressure = 0.05
  await row.getByText(actual(0.05), { exact: true }).waitFor()
  await page.waitForFunction(() => !document.querySelector('#tune-pressure-advance')?.disabled)
  await row.getByRole('button', { name: labels['tuning.disable_pa'], exact: true }).click()
  pressure = 0
  await row.getByText(actual(0), { exact: true }).waitFor()
  await page.waitForFunction(() => !document.querySelector('#tune-pressure-advance')?.disabled)
  assert.deepEqual(actions, ['tune:pressure-advance:0.05', 'tune:pressure-advance:0'])
  status = 2; state = 'printing'
  await page.waitForFunction(() => !document.querySelector('#tune-speed')?.disabled)
  await input.fill('0.032'); await apply.click(); pressure = 0.032
  await row.getByText(actual(0.032), { exact: true }).waitFor()
  await page.waitForFunction(() => !document.querySelector('#tune-pressure-advance')?.disabled)
  state = 'paused'
  await input.fill('0.04'); refuse = true; await apply.click()
  await page.getByText(/Fixture refusal/).waitFor()
  assert.equal(pressure, 0.032)
  refuse = false; pressure = null
  await page.waitForFunction(() => document.querySelector('#tune-pressure-advance')?.disabled)
  assert(await apply.isDisabled(), 'Missing readback must never fall back to a guessed configured value')
  pressure = 0.04237; age = 16
  await row.getByText(actual(0.04237), { exact: true }).waitFor()
  assert(await input.isDisabled())
  age = 0; status = 1; state = 'printing'
  await page.waitForTimeout(1200)
  assert(await input.isDisabled(), 'Mismatched native print state must refuse input')
  state = 'standby'; connected = false
  await page.waitForTimeout(1200); assert(await input.isDisabled())
  connected = true
  await page.waitForFunction(() => !document.querySelector('#tune-pressure-advance')?.disabled)
  for (const width of [1440, 768, 390]) {
    const geometry = await settledViewport(page, width)
    assert(geometry.width <= geometry.viewport, `Overflow at ${width}: ${JSON.stringify(geometry)}`)
    const bounds = await row.boundingBox(); assert(bounds && bounds.width > 100)
    if (process.env.CC2_PA_SCREENSHOTS) {
      await mkdir(process.env.CC2_PA_SCREENSHOTS, { recursive: true })
      await page.locator('.cc2-tuning').screenshot({ path: path.join(process.env.CC2_PA_SCREENSHOTS, `pressure-advance-${width}.png`) })
    }
  }
  assert(actions.every(action => action.startsWith('tune:pressure-advance:')))
  assert.deepEqual(errors, [])
  console.log('PASS: Control Pressure Advance precision, explicit apply/disable, fresh native readback, Idle/printing/paused, missing/stale/offline refusals and responsive card')
} finally {
  await browser?.close(); await server.close()
}
