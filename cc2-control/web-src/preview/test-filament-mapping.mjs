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
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0, 2], filaments: [{ tool: 0, color: '#FF0000', material: 'PLA' }, { tool: 2, color: '#00FF00', material: 'PETG' }] } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { telemetry: { result: { canvas_info: { canvas_list: [{ connected: 1, tray_list: [] }] } } } } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.route('**/api/gcode-files/print', r => {
    starts.push(r.request().postData())
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(`${origin}/#files`)
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('button', { name: 'Print', exact: true }).first().click()
  const dialog = page.getByRole('dialog')
  await dialog.waitFor()
  assert.match(await dialog.innerText(), /PLA · #FF0000/)
  assert.match(await dialog.innerText(), /PETG · #00FF00/)
  const selectors = dialog.locator('select')
  await selectors.nth(0).selectOption('2')
  await selectors.nth(1).selectOption('0')
  await dialog.getByRole('button', { name: 'Start print', exact: true }).click()
  await dialog.waitFor({ state: 'hidden' })
  assert.equal(starts[0].split('\n')[2], '0:2,2:0', 'file tools remain independently mapped to physical slots')
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0, 2] } }))
  await page.getByRole('button', { name: 'Print', exact: true }).first().click()
  await dialog.waitFor()
  assert.equal(await dialog.getByText('Colour/material not provided by the file', { exact: true }).count(), 2)
  assert.deepEqual(errors, [])
  console.log('PASS: file filament labels, sparse tool mapping and missing-metadata fallback')
} finally {
  await browser?.close()
  await server.close()
}
