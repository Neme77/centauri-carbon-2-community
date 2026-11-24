import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

// The choice of Spoolman's spools next to an entry of the list "Usage not sent to Spoolman": when Spoolman cannot be
// read at the first look, the choice fills itself after the next successful sync, without reloading the page.
// A sync that keeps failing does not make the page ask again and again. Fake data only (no printer, no Spoolman).
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const origin = server.resolvedUrls.local[0]
  const at = 1790000000
  const held = [{ id: 1, spool: 'aa00000000000001', name: 'Silk Blue', grams: 29.1, reason: 'server_changed', spoolman_id: '8', time: at - 3 * 86400, sending: false }]
  const spoolsOf = [
    { id: '21', name: 'Silk Blue', material: 'PLA Silk', color: '#2244CC', remaining: 640 },
    { id: '22', name: 'Green PLA', material: 'PLA', color: '#22AA44', remaining: 120.5 },
  ]
  // The printer's view of Spoolman: the last sync (time, result) and whether Spoolman answers the list of its spools.
  const spoolman = { time: 1000, result: 'timeout', up: false, catalogReads: 0, statusReads: 0 }
  const errors = []
  const context = await browser.newContext({ viewport: { width: 1440, height: 1400 } })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/spools', async r => {
    const response = await r.fetch()
    const data = await response.json()
    Object.assign(data, { spoolman_enabled: true, spoolman_url: 'http://192.0.2.5:7912', spoolman_has_auth: false, spoolman_gen: 5, spoolman_stale_links: 0, spoolman_held: held })
    await r.fulfill({ json: data })
  })
  await page.route('**/api/spoolman', r => {
    spoolman.statusReads++
    return r.fulfill({ json: { enabled: true, configured: true, last_sync: spoolman.time, result: spoolman.result } })
  })
  await page.route('**/api/spoolman/catalog/refresh', r => {
    spoolman.catalogReads++
    return r.fulfill({ status: 202, json: { ok: true } })
  })
  await page.route('**/api/spoolman/catalog', r =>
    r.fulfill({ json: spoolman.up ? { gen: 5, loading: false, error: '', spools: spoolsOf } : { gen: 5, loading: false, error: 'unreachable', spools: [] } }))
  await page.addInitScript(() => localStorage.setItem('cc2-spoolman-held-seen', '99'))

  const choice = () => page.locator('[data-held-id="1"]').getByRole('combobox')
  const failed = page.getByText('The spool list could not be read from Spoolman.', { exact: true })

  // 1. Spoolman is down at the first look: the choice stays empty and the page says why.
  await page.goto(origin + '#spools')
  await page.getByRole('heading', { name: 'Usage not sent to Spoolman (1 entry)', exact: true }).waitFor()
  await failed.waitFor()
  assert.deepEqual(await choice().locator('option').allTextContents(), ['Choose a spool'])
  assert.equal(spoolman.catalogReads, 1, 'one read of the list at the first look')

  // 2. The sync keeps failing: the page polls the printer, but does not ask Spoolman's list again.
  await page.waitForTimeout(25000)
  assert.equal(spoolman.catalogReads, 1, 'no new read while the sync fails')
  assert.ok(await failed.isVisible())

  // 3. Spoolman is back and a sync went through: the choice fills itself, no reload.
  spoolman.up = true
  spoolman.time = 2000
  spoolman.result = 'ok'
  await failed.waitFor({ state: 'detached', timeout: 40000 })
  assert.deepEqual(await choice().locator('option').allTextContents(), ['Choose a spool', 'Silk Blue · PLA Silk · 640.0 g', 'Green PLA · PLA · 120.5 g'])
  assert.equal(spoolman.catalogReads, 2, 'exactly one more read, after the sync')

  // 4. Nothing more is read once the list is there.
  await page.waitForTimeout(22000)
  assert.equal(spoolman.catalogReads, 2)

  assert.deepEqual(errors, [])
  console.log('PASS: Spoolman choice of spools: failed first read, no asking again while the sync fails, one read after the next good sync, no reload')
} finally {
  await browser?.close()
  await server.close()
}
