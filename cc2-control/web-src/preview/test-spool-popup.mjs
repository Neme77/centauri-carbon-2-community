import assert from 'node:assert/strict'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], mutations = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => { if (r.method() === 'POST' && r.url().includes('/api/spools/')) mutations.push(r.url()) })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.goto(`${origin}/#spools`)
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('button', { name: 'Turn on spool tracking' }).click()
  await page.getByText('Spool tracking is on.', { exact: true }).waitFor()
  await page.goto(`${origin}/#dashboard`)
  await page.evaluate(() => fetch('/__preview/spool-insert', { method: 'POST', body: 'slot=2' }))
  const dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'New filament in Slot 3' }).waitFor()
  const name = dialog.getByLabel('Name', { exact: true })
  await name.fill('Keep this spool draft')
  const before = mutations.length
  for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.mouse.click(2, 2)
    await page.waitForTimeout(200)
    assert.equal(await dialog.count(), 1, `outside click must retain the spool question at ${width}px`)
    assert.equal(await name.inputValue(), 'Keep this spool draft', 'outside click must retain edits')
    assert.equal(mutations.length, before, 'outside click must not dismiss, assign or save')
  }
  await dialog.getByRole('button', { name: 'Not now', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  await page.waitForTimeout(1500)
  assert.equal(await dialog.count(), 0, 'explicit Not now must still snooze the question')
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.goto(`${origin}/#spools`)
  await page.locator('[data-slot="2"]').getByRole('button', { name: 'Choose spool' }).click()
  await dialog.waitFor()
  // Focus is moved by the same after-paint effect that installs Escape.
  // Visible DOM alone does not prove that the keyboard handler is ready.
  await page.waitForFunction(() => document.querySelector('[role=dialog]')?.contains(document.activeElement))
  await page.mouse.click(2, 2)
  assert.equal(await dialog.count(), 1, 'manual spool chooser also keeps accidental outside clicks')
  await page.keyboard.press('Escape')
  await dialog.waitFor({ state: 'detached' })
  assert.deepEqual(errors, [])
  console.log('PASS: spool question outside clicks retain draft on desktop/mobile, no mutations, explicit Not now and Escape still close')
} finally {
  await browser?.close()
  await server.close()
}
