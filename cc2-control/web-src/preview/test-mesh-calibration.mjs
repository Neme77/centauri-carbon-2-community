import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Bed Levelling > Mesh calibration runs on CC2 Control: the page sends the side, bed temperature, soak time,
// nozzle and plate once, then follows the stages (homing, heating, soaking, probing, saving), can stop the run
// before the probing and reports the outcome. The preview moves the run one stage on per step request.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], plateCalls = [], consoleCalls = []
  let meshLoads = 0
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    const url = new URL(r.url()).pathname
    if (url === '/api/mesh') meshLoads++
    if (url.startsWith('/api/console/')) consoleCalls.push(url)
    if (r.method() === 'POST' && url.startsWith('/api/plates/')) {
      assert.equal(r.headers()['x-cc2-request'], '1', 'plate changes carry the mutation marker')
      plateCalls.push([url, r.postData()])
    }
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  let homed = ''
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.motion = { ...data.motion, homed_axes: homed }
    await route.fulfill({ response, json: data })
  })
  const step = (body = '') => page.evaluate(b => fetch('/__preview/calibration-step', { method: 'POST', headers: { 'X-CC2-Request': '1' }, body: b }), body)
  const progress = page.locator('[data-calibration]')
  const toast = text => page.getByText(text, { exact: true }).waitFor()

  await page.goto(`${origin}/#bed`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')]) // the scenario reloads the page
  // Labels wrap their controls, so address the control inside each label.
  const field = text => page.locator('label', { hasText: text }).locator('select, input')
  const side = field('Calibration plate side'), temperature = field('Bed temperature, °C')
  const soak = field('Soak, min'), target = field('Save the result to plate')
  const run = page.getByRole('button', { name: /Run Bed Mesh Calibration/ })
  const dialog = page.getByRole('dialog')
  await side.waitFor()
  await side.selectOption('default')
  assert.equal(await target.inputValue(), 'a1b2c3d4e5f60718', 'the mounted Side A plate takes the result')
  assert.equal(await soak.inputValue(), '10', 'the bed is held for ten minutes unless chosen otherwise')

  // Invalid values never reach the printer.
  await soak.fill('61')
  await run.click()
  await toast('Use a whole soak time from 0 to 60 minutes.')
  await temperature.fill('120')
  await soak.fill('10')
  await run.click()
  await toast('Use a whole bed temperature from 40 to 110 °C.')
  assert.equal(await dialog.count(), 0)
  assert.equal(plateCalls.length, 0)

  // Unhomed, 80 °C, a ten-minute soak and the mounted plate: one request starts the run on the printer.
  await temperature.fill('80')
  await run.click()
  for (const line of [
    'Start a new bed mesh calibration at 80 °C?',
    'Nozzle: 0.4 brass',
    'The result becomes the 80 °C measurement of “Smooth PEI”.',
    'The bed heats to 80 °C and is held there for 10 min before probing.',
    'The printer is not homed, so it homes X, Y and Z first.',
  ])
    await dialog.getByText(line, { exact: false }).waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await progress.waitFor()
  assert.deepEqual(plateCalls, [['/api/plates/calibrate', 'A\n80\n10\ndd00000000000004\na1b2c3d4e5f60718']])
  assert.equal(consoleCalls.length, 0, 'the page sends no console commands itself')
  await page.getByText('Homing X, Y and Z…', { exact: true }).waitFor()
  assert.ok(await run.isDisabled() && (await temperature.isDisabled()), 'nothing else starts while it runs')
  const stages = [
    ['heating', 'Heating the bed: 25 °C of 80 °C'],
    ['soaking', 'Holding the bed at 80 °C: 10:00 left'],
    ['probing', 'Probing Side A at 80 °C…'],
    ['saving', 'Saving the measurement to “Smooth PEI”…'],
  ]
  for (const [stage, text] of stages) {
    await step()
    await page.locator(`[data-calibration="${stage}"]`).waitFor()
    await page.getByText(text, { exact: true }).waitFor()
    const stop = progress.getByRole('button', { name: 'Stop', exact: true })
    assert.equal(await stop.count(), ['probing', 'saving'].includes(stage) ? 0 : 1, `Stop while ${stage}`)
  }
  const loads = meshLoads
  await step()
  await toast('Saved to “Smooth PEI” as its 80 °C measurement.')
  await progress.waitFor({ state: 'detached' })
  for (let i = 0; i < 50 && meshLoads === loads; i++) await page.waitForTimeout(100)
  assert.ok(meshLoads > loads, 'the new mesh is read once the run ends')
  assert.ok(!(await run.isDisabled()))

  // Homed, no plate, no soak: the confirmation says less; stopping while heating switches the bed off.
  homed = 'xyz'
  await page.waitForTimeout(2500)
  await soak.fill('0')
  await target.selectOption('')
  plateCalls.length = 0
  await run.click()
  await dialog.getByText('Start a new bed mesh calibration at 80 °C?', { exact: false }).waitFor()
  for (const line of ['The printer is not homed', 'is held there for', 'The result becomes'])
    assert.equal(await dialog.getByText(line, { exact: false }).count(), 0, line)
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await progress.waitFor()
  assert.deepEqual(plateCalls, [['/api/plates/calibrate', 'A\n80\n0\ndd00000000000004\n']])
  await step()
  await page.locator('[data-calibration="heating"]').waitFor()
  await progress.getByRole('button', { name: 'Stop', exact: true }).click()
  await toast('The calibration was stopped and the bed heater switched off.')
  assert.equal(plateCalls.at(-1)[0], '/api/plates/calibrate/cancel')
  await progress.waitFor({ state: 'detached' })

  // A run that fails while homing says so.
  await run.click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await progress.waitFor()
  await step('fail')
  await toast('Homing did not finish, so the calibration was not started.')
  assert.deepEqual(errors, [])
  console.log('PASS: mesh calibration runs on CC2 Control with temperature, soak, nozzle and plate; stages, stop, outcome and mesh reload')
} finally { await browser?.close(); await server.close() }
