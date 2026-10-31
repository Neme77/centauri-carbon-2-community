import assert from 'node:assert/strict'
import { readFile, mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
const en = JSON.parse(await readFile(path.join(root, 'public/locales/en.json'), 'utf8'))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], posts = []
  page.on('pageerror', error => errors.push(error.message))
  let state = { editable: true, min: 60, max: 350, configured: 80, override: 200, pending_restart: false, pending_conflict: false, pending_value: null, restart: 'none' }
  await page.route('**/api/canvas/unload', async route => {
    const request = route.request()
    if (request.method() === 'POST') {
      posts.push(request.postData())
      const value = request.postData() === 'default' ? null : Number(request.postData())
      state = { ...state, override: value, pending_restart: true, pending_conflict: false, pending_value: value ?? 0 }
    }
    await route.fulfill({ json: state })
  })
  await page.route('**/api/canvas/unload/restart', async () => {
    assert.fail('This UI test never confirms or sends a restart')
  })
  await page.goto(origin + '/#canvas')
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')])
  const box = page.locator('[data-canvas-unload]')
  const input = box.locator('input')
  const save = box.getByRole('button', { name: en['canvas.unload_save'], exact: true })
  await input.fill('300')
  await save.click()
  await page.getByRole('button', { name: en['common.cancel'], exact: true }).click()
  await box.getByText(en['canvas.unload_pending_safety'], { exact: false }).waitFor()
  assert.deepEqual(posts, ['300'])

  // The original screen saves its boot-time pull-back plus a new Z offset.
  state = { ...state, override: 200, pending_restart: false, pending_conflict: true }
  await box.getByRole('alert').filter({ hasText: en['canvas.unload_conflict'] }).waitFor({ timeout: 10000 })
  assert.equal(await box.getByRole('button', { name: en['canvas.unload_restart'], exact: true }).count(), 0)
  assert.equal(await input.inputValue(), '300', 'polling preserves the requested value')
  assert.deepEqual(posts, ['300'], 'a native conflict never causes an automatic rewrite')
  await save.click()
  await page.getByRole('button', { name: en['common.cancel'], exact: true }).click()
  assert.deepEqual(posts, ['300', '300'])
  assert.equal(await box.getByRole('alert').count(), 0)

  // The same value may be explicitly re-saved to resolve legacy/unknown tracking.
  state = { ...state, pending_restart: false, pending_conflict: true, pending_value: null }
  await box.getByRole('alert').waitFor({ timeout: 10000 })
  assert.equal(await save.isEnabled(), true)
  await save.click()
  await page.getByRole('button', { name: en['common.cancel'], exact: true }).click()
  assert.deepEqual(posts, ['300', '300', '300'])

  await box.getByRole('button', { name: en['canvas.unload_default'], exact: true }).click()
  await page.getByRole('button', { name: en['common.cancel'], exact: true }).click()
  state = { ...state, override: 200, pending_restart: false, pending_conflict: true }
  await box.getByRole('alert').waitFor({ timeout: 10000 })
  assert.equal(await input.inputValue(), '80', 'the requested default remains visible after a native overwrite')
  await save.click()
  await page.getByRole('button', { name: en['common.cancel'], exact: true }).click()
  assert.deepEqual(posts.slice(-2), ['default', 'default'], 're-saving a pending default must remove the override')

  await page.route('**/api/plates', async route => {
    const response = await route.fetch()
    const library = await response.json()
    await route.fulfill({ json: { ...library, pending: '1111111111111111', pending_conflict: true } })
  })
  await page.goto(origin + '/#bed/plates')
  await page.getByText(en['plates.pending_save_conflict'], { exact: true }).waitFor()
  const output = process.argv[2]
  if (output) {
    await mkdir(output, { recursive: true })
    await page.screenshot({ path: path.join(output, 'pending-mesh-conflict.png'), fullPage: true })
  }
  assert.deepEqual(errors, [])
  console.log('PASS #267 browser: pending warning, external overwrite detected while open, no false restart or automatic rewrite, retained input, explicit re-save and plate conflict banner')
} finally {
  if (browser) await browser.close()
  await server.close()
}
