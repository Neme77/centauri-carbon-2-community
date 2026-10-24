import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
// machine: 1 idle, 2 printing, 3 paused. connected=false keeps the cached machine fields, as the
// backend does after an MQTT disconnect; down=true makes /api/printer itself fail.
let machine = 1, connected = true, down = false, mode = null, reject = false
const commands = [], errors = []
const names = { 1: 'Idle', 2: 'Printing', 3: 'Paused' }
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', r => down ? r.fulfill({ status: 503, body: 'unavailable' }) : r.fulfill({ json: {
    connected, last_message_age: connected ? 0 : 120, motor_mode: mode, experiments: { quiet_motors: true },
    machine: { status: machine, status_name: names[machine], progress: 20 },
    print: { filename: machine === 1 ? '' : 'fixture.gcode', state: machine === 1 ? 'idle' : machine === 3 ? 'paused' : 'printing' },
    extruder: { temperature: 25, target: 0 }, heater_bed: { temperature: 25, target: 0 },
  } }))
  await page.route('**/api/control', r => {
    const action = r.request().postData()
    commands.push(action)
    if (reject) return r.fulfill({ status: 409, body: 'printer telemetry is stale' })
    // Telemetry reports the requested mode only after the backend accepted it.
    if (action === 'motors:quiet') mode = 'quiet'
    if (action === 'motors:normal') mode = 'normal'
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  const quiet = page.getByRole('button', { name: 'Quiet', exact: true })
  const normal = page.getByRole('button', { name: 'Normal', exact: true })
  const warning = page.getByText('Quiet mode is on for this print', { exact: false })
  const pressed = (name, on) => page.waitForFunction(([name, on]) => [...document.querySelectorAll('button[aria-pressed]')]
    .some(b => b.textContent.trim() === name && b.getAttribute('aria-pressed') === String(on)), [name, on])
  // Both buttons exist and are all enabled or all disabled.
  const buttons = disabled => page.waitForFunction(disabled => {
    const b = [...document.querySelectorAll('button')].filter(b => ['Quiet', 'Normal'].includes(b.textContent.trim()))
    return b.length === 2 && b.every(b => b.disabled === disabled)
  }, disabled)
  const idleAgain = async () => {
    machine = 1; connected = true; down = false
    await buttons(false)
  }

  await quiet.waitFor()
  await buttons(false)
  await pressed('Quiet', false)
  await pressed('Normal', false)

  // Quiet: cancel sends nothing, the confirmation names the cap.
  await quiet.click()
  await page.getByText('Speed is capped at 80 mm/s and acceleration at 2000 mm/s²', { exact: false }).waitFor()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.waitForTimeout(300)
  assert.deepEqual(commands, [])
  await pressed('Quiet', false)

  // Quiet: confirm sends exactly one action and the pressed state follows telemetry.
  await quiet.click()
  await page.getByRole('button', { name: 'Confirm', exact: true }).click()
  await pressed('Quiet', true)
  assert.deepEqual(commands, ['motors:quiet'])
  assert.equal(await warning.count(), 0, 'no print warning while idle')

  // Each unavailable state starts from enabled idle, so the disabled wait observes that state.
  for (const state of [2, 3]) {
    machine = state
    await buttons(true)
    await warning.waitFor()
    await idleAgain()
    assert.equal(await warning.count(), 0, 'print warning clears when idle')
  }
  connected = false
  await buttons(true)
  await idleAgain()
  down = true
  await buttons(true)
  await idleAgain()
  assert.deepEqual(commands, ['motors:quiet'])

  // Normal: cancel sends nothing and names the fixed restore values; confirm sends exactly one action.
  await normal.click()
  await page.getByText('restore the stock 500 mm/s and 20000 mm/s²', { exact: false }).waitFor()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.waitForTimeout(300)
  assert.deepEqual(commands, ['motors:quiet'])
  await normal.click()
  await page.getByRole('button', { name: 'Confirm', exact: true }).click()
  await pressed('Normal', true)
  await pressed('Quiet', false)
  assert.deepEqual(commands, ['motors:quiet', 'motors:normal'])

  // UI rendering only: when telemetry reports Normal after a Quiet print, the page follows it.
  mode = 'quiet'
  await pressed('Quiet', true)
  machine = 2
  await warning.waitFor()
  machine = 1
  mode = 'normal'
  await pressed('Normal', true)
  assert.equal(await warning.count(), 0)

  // A refused request (e.g. stale telemetry) shows an error and leaves the mode unchanged.
  reject = true
  await quiet.click()
  await page.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('printer telemetry is stale', { exact: false }).waitFor()
  assert.deepEqual(commands, ['motors:quiet', 'motors:normal', 'motors:quiet'])
  await pressed('Normal', true)
  await pressed('Quiet', false)

  for (const width of [768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(100)
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal overflow')
  }
  assert.deepEqual(errors, [])
  console.log('PASS: motor mode Quiet/Normal cancel and confirm, telemetry-driven state, unavailable while printing, paused, MQTT-disconnected or unreachable, print warning, refused request')
} finally {
  await browser?.close()
  await server.close()
}
