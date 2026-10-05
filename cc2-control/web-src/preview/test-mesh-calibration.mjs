import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Bed Levelling > Mesh calibration through the console: an unhomed printer is homed first (G28, then
// the printer must report X, Y and Z homed), a homed printer calibrates at once, and failed homing
// never starts the calibration.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  const calibrate = 'BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60'
  // The simulated printer: its homing state, the commands the console accepted and the console state.
  let homed = '', failHoming = false, served = { homed: 0, unhomed: 0 }, meshLoads = 0
  let sent = [], status = { command: '', busy: false, completed: false, success: false, generation: 0, output: '' }
  page.on('request', r => { if (new URL(r.url()).pathname === '/api/mesh') meshLoads++ })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.motion = { ...data.motion, homed_axes: homed }
    served[homed ? 'homed' : 'unhomed']++
    await route.fulfill({ response, json: data })
  })
  await page.route('**/api/console/command', async route => {
    assert.equal(route.request().headers()['x-cc2-request'], '1', 'console commands carry the mutation marker')
    const command = route.request().postData().trim()
    sent.push(command)
    status = { command, busy: true, completed: false, success: false, generation: status.generation + 1, output: '' }
    await route.fulfill({ status: 202, json: { accepted: true } })
  })
  // A command is reported busy once, then finished; a successful G28 homes the axes.
  await page.route('**/api/console', async route => {
    const reply = { ...status }
    if (status.busy) {
      const ok = !(status.command === 'G28' && failHoming)
      status = { ...status, busy: false, completed: true, success: ok }
      if (status.command === 'G28' && ok) homed = 'xyz'
    }
    await route.fulfill({ json: reply })
  })
  const until = async (test, what) => {
    for (let i = 0; i < 200; i++) {
      if (await test()) return
      await page.waitForTimeout(100)
    }
    assert.fail(what)
  }

  await page.goto(`${origin}/#bed`)
  await page.selectOption('#preview-scene', 'idle')
  // The label wraps its select, so its text includes the options: address the select inside it.
  const side = page.locator('label', { hasText: 'Calibration plate side' }).locator('select')
  await side.waitFor()
  const run = page.getByRole('button', { name: /Run Bed Mesh Calibration/ })
  const dialog = page.getByRole('dialog')
  await side.selectOption('default')

  // Not homed: the confirmation says so, G28 runs first and the calibration follows once homed.
  await until(() => served.unhomed > 0, 'the page has seen the unhomed printer')
  await run.click()
  await dialog.getByText('The printer is not homed, so it homes X, Y and Z first.', { exact: false }).waitFor()
  const before = meshLoads
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await until(() => sent.length === 2, 'G28 and the calibration were sent')
  assert.deepEqual(sent, ['G28', calibrate])
  await until(() => meshLoads > before, 'the new mesh is loaded after the calibration')
  await until(async () => !(await run.isDisabled()), 'the button is ready again')

  // Homed: no homing line, the calibration starts at once.
  sent = []
  const homedSeen = served.homed
  await until(() => served.homed > homedSeen, 'the page has seen the homed printer')
  await page.waitForTimeout(300)
  await run.click()
  await dialog.waitFor()
  assert.equal(await dialog.getByText('The printer is not homed', { exact: false }).count(), 0)
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await until(() => sent.length === 1, 'the calibration was sent')
  assert.deepEqual(sent, [calibrate])
  await until(async () => !(await run.isDisabled()), 'the button is ready again')

  // Homing fails: an error, and nothing that would probe.
  sent = []
  homed = ''
  failHoming = true
  const unhomedSeen = served.unhomed
  await until(() => served.unhomed > unhomedSeen, 'the page has seen the printer unhomed again')
  await page.waitForTimeout(300)
  await run.click()
  await dialog.getByText('The printer is not homed, so it homes X, Y and Z first.', { exact: false }).waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('Homing did not finish, so the calibration was not started.', { exact: true }).waitFor()
  await page.waitForTimeout(1500)
  assert.deepEqual(sent, ['G28'])
  assert.deepEqual(errors, [])
  console.log('PASS: mesh calibration homes an unhomed printer first, calibrates a homed one at once and stops when homing fails')
} finally { await browser?.close(); await server.close() }
