import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], starts = []
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  let analyses = 0, releaseAnalysis, markStarted
  const hold = new Promise(resolve => { releaseAnalysis = resolve })
  const started = new Promise(resolve => { markStarted = resolve })
  await page.route('**/api/gcode-files/inspect', async r => {
    analyses++; markStarted(); await hold
    return r.fulfill({ json: { tools: [0, 2], filaments: [{ tool: 0, color: '#FF0000', material: 'PLA' }, { tool: 2, color: '#00FF00', material: 'PETG' }] } })
  })
  await page.route('**/api/canvas', r => r.fulfill({ json: { telemetry: { result: { canvas_info: { canvas_list: [{ connected: 1, tray_list: [
    { tray_id: 0, filament_type: 'PLA', filament_color: '#FF0000' },
    { tray_id: 1, filament_type: 'PLA', filament_color: '#FFFFFF' },
    { tray_id: 2, filament_type: 'PETG', filament_color: [0, 255, 0] },
    { tray_id: 3, filament_type: 'PLA' },
  ] }] } } } } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.route('**/api/gcode-files/print', r => {
    starts.push(r.request().postData())
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(`${origin}/#files`)
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('button', { name: 'Print', exact: true }).first().click()
  await started
  await page.getByText("Analysing file\u2026", { exact: true }).waitFor()
  await page.getByRole('button', { name: 'Print', exact: true }).first().click()
  assert.equal(analyses, 1, 'duplicate clicks cannot queue another analysis')
  assert.equal(starts.length, 0, 'analysis cannot start a print')
  releaseAnalysis()
  const dialog = page.getByRole('dialog')
  await dialog.waitFor()
  assert.match(await dialog.innerText(), /PLA · #FF0000/)
  assert.match(await dialog.innerText(), /PETG · #00FF00/)
  const tool0 = dialog.getByRole('group', { name: 'Filament T0', exact: true })
  const tool2 = dialog.getByRole('group', { name: 'Filament T2', exact: true })
  assert.equal(await tool0.getByRole('radio').count(), 4)
  assert.match(await tool0.innerText(), /#FFFFFF/)
  assert.equal(await tool0.locator('input:checked').count(), 0, 'mapping requires manual confirmation')
  await dialog.getByRole('button', { name: 'Start print', exact: true }).click()
  assert.equal(starts.length, 0, 'unassigned tools cannot start a print')
  await tool0.getByRole('radio', { name: /Slot 3/ }).check()
  await tool2.getByRole('radio', { name: /Slot 1/ }).check()
  assert.equal(await tool0.getByRole('radio', { name: /Slot 3/ }).isChecked(), true)
  assert.equal(await tool2.getByRole('radio', { name: /Slot 1/ }).isChecked(), true)
  assert.equal(await tool0.getByRole('radio', { name: /Slot 4/ }).locator('..').locator('[style]').count(), 0, 'unknown colours do not use an invented swatch')
  if (process.env.CC2_SCREENSHOTS) await page.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, 'filament-mapping-desktop.png') })
  await dialog.getByRole('button', { name: 'Start print', exact: true }).click()
  await dialog.waitFor({ state: 'hidden' })
  assert.equal(starts[0].split('\n')[2], '0:2,2:0', 'file tools remain independently mapped to physical slots')
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0, 2] } }))
  await page.getByRole('button', { name: 'Print', exact: true }).first().click()
  await dialog.waitFor()
  assert.equal(await dialog.getByText('Colour/material not provided by the file', { exact: true }).count(), 2)
  await page.setViewportSize({ width: 390, height: 844 })
  assert.equal(await dialog.evaluate(el => el.scrollWidth <= el.clientWidth), true, 'mapping fits a mobile dialog')
  await dialog.getByRole('group', { name: 'Filament T0', exact: true }).getByRole('radio', { name: /Slot 2/ }).check()
  assert.equal(starts.length, 1, 'choosing a slot never starts a print')
  if (process.env.CC2_SCREENSHOTS) await page.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, 'filament-mapping-mobile.png') })
  assert.deepEqual(errors, [])
  console.log('PASS: slow analysis indicator, duplicate-click guard, confirmation, filament labels and mapping')
} finally {
  await browser?.close()
  await server.close()
}
