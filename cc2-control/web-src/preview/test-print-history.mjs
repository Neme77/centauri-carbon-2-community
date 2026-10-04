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
  const errors = [], starts = [], deletions = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => { if (r.url().endsWith('/api/history/delete')) deletions.push(r.postData()) })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0] } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.route('**/api/gcode-files/print', r => {
    starts.push(r.request().postData())
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(`${origin}/#files`)
  await page.selectOption('#preview-scene', 'idle')
  for (const [timelapse, calibrate] of [[true, false], [false, true]]) {
    await page.getByRole('button', { name: 'Print', exact: true }).first().click()
    const dialog = page.getByRole('dialog')
    await dialog.waitFor()
    const checkbox = dialog.getByRole('checkbox', { name: 'Enable timelapse', exact: true })
    assert.equal(await checkbox.isChecked(), false, 'each print starts with an explicit unchecked option')
    if (timelapse) await checkbox.check()
    if (calibrate) await dialog.getByRole('checkbox', { name: 'Calibrate bed before printing', exact: true }).check()
    await dialog.getByRole('button', { name: 'Start print', exact: true }).click()
    await dialog.waitFor({ state: 'hidden' })
    const lines = starts.at(-1).split('\n')
    assert.equal(lines[4], calibrate ? 'calibrate' : 'saved')
    assert.equal(lines[5], timelapse ? '1' : '0')
  }
  await page.goto(`${origin}/#history`)
  const name = 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode'
  const one = page.getByRole('button', { name: `Delete ${name}`, exact: true })
  await one.waitFor()
  await one.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(deletions.length, 0, 'Cancel must not send a deletion')
  await one.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await one.waitFor({ state: 'hidden' })
  assert.deepEqual(deletions, ['preview-1'])
  const clear = page.getByRole('button', { name: 'Clear history', exact: true })
  await clear.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('main button[aria-label^="Delete "]'))
  assert.equal(deletions[1], 'preview-2\npreview-3\npreview-4')
  await page.selectOption('#preview-scene', 'printing')
  await one.waitFor()
  assert.equal(await clear.isEnabled(), false, 'bulk deletion is disabled during printing')
  assert.equal(await one.isEnabled(), false, 'entry deletion is disabled during printing')
  assert.deepEqual(errors, [])
  console.log('PASS: print popup sends explicit timelapse on/off for saved/calibrated starts; history cancellation, confirmed single/bulk deletion and printing guards')
} finally {
  await browser?.close()
  await server.close()
}
