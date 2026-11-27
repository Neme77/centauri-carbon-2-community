import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

// Exercise the actual card: fake HTTP only; no printer, RFID or Spoolman server.
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  let busy = false, result = 'ok', enabled = true, posts = 0, libraryReads = 0, statusReads = 0, fail = false, imported = false
  let release
  await page.route('**/api/spools', async route => {
    libraryReads++
    const response = await route.fetch()
    const data = await response.json()
    Object.assign(data, { enabled: true, spoolman_enabled: enabled, spoolman_url: 'http://192.0.2.5:7912', spoolman_gen: 5, spoolman_held: [], spoolman_stale_links: 0 })
    if (imported) data.spools.push({ ...data.spools[0], id: 'new-manual-sync', name: 'Fresh sync spool', spoolman_id: '12345' })
    await route.fulfill({ json: data })
  })
  await page.route('**/api/spoolman', route => {
    assert.equal(route.request().method(), 'GET')
    statusReads++
    return route.fulfill({ json: { enabled, configured: true, busy, last_sync: 1790000000, result, http: 0, list: {} } })
  })
  await page.route('**/api/spoolman/sync', async route => {
    posts++
    assert.equal(route.request().method(), 'POST')
    assert.equal(route.request().headers()['x-cc2-request'], '1')
    if (fail) return route.fulfill({ status: 409, json: { error: 'Spoolman integration is disabled', code: 'spoolman_off' } })
    busy = true
    await new Promise(resolve => { release = resolve })
    return route.fulfill({ status: 202, json: { ok: true } })
  })
  const open = async () => {
    await page.goto(server.resolvedUrls.local[0] + '#spools')
    await page.getByRole('button', { name: 'Synchronize spools', exact: true }).waitFor()
  }
  const sync = page.getByRole('button', { name: 'Synchronize spools', exact: true })
  const line = page.locator('[data-spoolman-status]')
  await open()
  await sync.click()
  await page.waitForFunction(() => document.querySelector('button[aria-busy]')?.disabled)
  assert.equal(posts, 1)
  release()
  await line.filter({ hasText: 'Synchronizing with Spoolman…' }).waitFor()
  assert.equal(await sync.isDisabled(), true)
  await sync.evaluate(button => button.click())
  assert.equal(posts, 1, 'disabled button cannot start a second request')
  const before = libraryReads
  busy = false
  imported = true
  await line.filter({ hasText: 'Synchronised with Spoolman' }).waitFor({ timeout: 12000 })
  await page.getByText('Fresh sync spool', { exact: true }).first().waitFor()
  assert.ok(libraryReads > before, 'completion refreshes the local spool library')
  assert.equal(await sync.isEnabled(), true)
  // Draft settings must be saved, not silently substituted for the saved server.
  const url = page.locator('input[placeholder="http://192.168.1.50:7912"]')
  await url.fill('http://192.0.2.9:7912')
  assert.equal(await sync.isDisabled(), true)
  await url.fill('http://192.0.2.5:7912')
  assert.equal(await sync.isEnabled(), true)
  // Refusal is shown without pretending that a sync succeeded.
  fail = true
  await sync.click()
  await page.getByText(/Rejected: Spoolman sync is off/).first().waitFor()
  assert.equal(await sync.isEnabled(), true)
  assert.equal(posts, 2)
  // Reload during a cycle recovers the busy state; an automatic cycle does too.
  fail = false
  busy = true
  await page.reload()
  await line.filter({ hasText: 'Synchronizing with Spoolman…' }).waitFor()
  assert.equal(await sync.isDisabled(), true)
  busy = false
  result = 'unreachable'
  await line.filter({ hasText: 'Spoolman cannot be reached.' }).waitFor({ timeout: 12000 })
  assert.equal(await sync.isEnabled(), true)
  await page.setViewportSize({ width: 390, height: 844 })
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'phone layout does not scroll sideways')
  enabled = false
  await page.reload()
  await page.getByRole('heading', { name: 'Spool Tracking', exact: true }).waitFor()
  assert.equal(await sync.count(), 0, 'disabled integration keeps its card hidden')
  // No page-specific fast poll survives navigation away.
  await page.goto(server.resolvedUrls.local[0] + '#dashboard')
  await page.waitForTimeout(1000)
  const stopped = statusReads
  await page.waitForTimeout(2500)
  assert.equal(statusReads, stopped)
  assert.deepEqual(errors, [])
  console.log('PASS: manual Spoolman sync: one POST, progress, fresh imports, refusals, drafts, reload, phone and polling cleanup')
  await page.unrouteAll({ behavior: 'ignoreErrors' })
} finally {
  if (browser) await browser.close()
  await server.close()
}
