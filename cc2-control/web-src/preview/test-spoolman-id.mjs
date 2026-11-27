import assert from 'node:assert/strict'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { mkdir } from 'node:fs/promises'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, page, releaseCloseProbe
let closeProbeStarted = false, closeProbeFinished = false
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  if (process.env.CC2_AXE_PATH) await page.addInitScript({ path: process.env.CC2_AXE_PATH })
  const errors = [], assignments = []
  const accessibility = []
  const audit = async label => {
    if (!process.env.CC2_AXE_PATH) return
    const result = await page.evaluate(async () => {
      const report = await window.axe.run()
      return report.violations.map(v => ({ id: v.id, impact: v.impact, targets: v.nodes.map(n => n.target) }))
    })
    accessibility.push({ label, violations: result })
    assert.deepEqual(result, [], `accessibility: ${label}`)
  }
  const ids = ['aa00000000000001', 'aa00000000000004', 'aa00000000000006', 'aa00000000000002', 'aa00000000000007']
  const remoteIds = ['123456789012345', '42', '43', '', '']
  let assigned = ''
  let revision = 70
  page.on('pageerror', error => errors.push(error.message))
  await page.route('**/api/preferences', route => route.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async route => {
    const response = await route.fetch()
    const data = await response.json()
    data.spools = { enabled: true, revision, questions: [] }
    await route.fulfill({ json: data })
  })
  await page.route('**/api/spools', async route => {
    const response = await route.fetch()
    const data = await response.json()
    const base = data.spools[0]
    data.enabled = true
    data.revision = revision
    data.spools = ids.map((id, i) => ({ ...base, id, name: 'Identical PLA', brand: 'ELEGOO', material: 'PLA', color: '#EF5350', remaining: 1000, net: 1000, used: 0, note: '', archived: false, spoolman_id: remoteIds[i] }))
    data.slots.forEach((slot, i) => {
      slot.spool = i === 0 ? ids[0] : i === 1 ? assigned : ''
      slot.last = ''
      slot.question = ''
      if (slot.printer) Object.assign(slot.printer, { status: 1, type: 'PLA', name: 'PLA', brand: 'ELEGOO', color: '#EF5350' })
    })
    await route.fulfill({ json: data })
  })
  await page.route('**/api/spools/assign', async route => {
    const body = route.request().postData()
    assignments.push(body)
    const fields = Object.fromEntries(body.split('\n').map(line => line.split('=')))
    assert.equal(fields.slot, '1')
    assert(ids.includes(fields.spool), 'assign the local spool key, never the remote Spoolman ID')
    assigned = fields.spool
    revision++
    await route.fulfill({ json: { ok: true } })
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin + '/#spools')
  if (await page.locator('#preview-scene').inputValue() !== 'idle') {
    await Promise.all([
      page.waitForNavigation({ waitUntil: 'domcontentloaded' }),
      page.selectOption('#preview-scene', 'idle'),
    ])
  }
  await page.locator('[data-slot="0"] [data-spoolman-id="123456789012345"]').waitFor()
  await audit('loaded Spoolman ID')
  assert.equal(await page.locator('[data-slot="1"] [data-spoolman-id]').count(), 0, 'unassigned slots have no invented ID')
  const search = page.locator('input[type=search]').first()
  for (const query of ['42', '#42', 'Spoolman #42']) {
    await search.fill(query)
    await page.locator(`[data-spool="${ids[1]}"] [data-spoolman-id="42"]`).waitFor()
    assert.equal(await page.locator(`[data-spool="${ids[2]}"]`).count(), 0, 'ID search excludes the identical spool with another ID')
  }
  await search.fill('Identical PLA')
  await page.locator(`[data-spool="${ids[3]}"]`).waitFor()
  assert.equal(await page.locator(`[data-spool="${ids[3]}"] [data-spoolman-id]`).count(), 0, 'unlinked local spools have no badge')
  await search.fill('')
  await page.locator('[data-slot="1"]').getByRole('button', { name: 'Choose spool', exact: true }).click()
  const dialog = page.getByRole('dialog')
  await dialog.locator(`[data-option="${ids[1]}"] [data-spoolman-id="42"]`).waitFor()
  await dialog.locator(`[data-option="${ids[2]}"] [data-spoolman-id="43"]`).waitFor()
  // The two unlinked sealed twins still use one option; the linked twins never do.
  assert.equal(await dialog.locator(`[data-option="${ids[3]}"]`).count() + await dialog.locator(`[data-option="${ids[4]}"]`).count(), 1)
  await dialog.getByRole('button', { name: /Show all/ }).click()
  const treeSearch = dialog.locator('input[type=search]')
  await treeSearch.fill('#42')
  const tree = dialog.getByTestId('spool-tree')
  const radio = tree.getByRole('radio', { name: /Spoolman #42/ })
  await radio.check()
  assert.equal(await tree.locator('[data-spoolman-id="43"]').count(), 0)
  for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal page overflow')
    const fits = await dialog.evaluate(node => node.scrollWidth <= node.clientWidth + 1)
    assert(fits, 'the chooser fits on mobile')
    await audit(`Spoolman chooser ${width}`)
    await mkdir(path.join(root, 'preview/screenshots'), { recursive: true })
    await page.screenshot({ path: path.join(root, `preview/screenshots/spoolman-id-${width}.png`), fullPage: true })
  }
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  await page.locator('[data-slot="1"] [data-spoolman-id="42"]').waitFor()
  assert.deepEqual(assignments, [`slot=1\nspool=${ids[1]}`])
  await page.goto(origin + '/#canvas')
  await page.locator('[data-canvas-spool-name]').filter({ hasText: 'Spoolman #42' }).waitFor()
  await audit('Canvas Spoolman ID')
  // Hold a real route.fetch response open across teardown. Without draining routes,
  // browser.close races the handler even though all functional checks have passed.
  let entered
  const started = new Promise(resolve => { entered = resolve })
  const released = new Promise(resolve => { releaseCloseProbe = resolve })
  await page.route('**/__spoolman-close-probe', async route => {
    const response = await route.fetch({ url: origin + '/api/printer' })
    closeProbeStarted = true
    entered()
    await released
    await route.fulfill({ response })
    closeProbeFinished = true
  })
  await page.evaluate(() => { void fetch('/__spoolman-close-probe').catch(() => {}) })
  let probeTimeout
  try {
    await Promise.race([started, new Promise((_, reject) => {
      probeTimeout = setTimeout(() => reject(new Error('teardown probe did not enter its route')), 5000)
    })])
  } finally {
    clearTimeout(probeTimeout)
  }
  if (accessibility.length) console.log(`PASS: ${accessibility.length} axe accessibility checks`)
  assert.deepEqual(errors, [])
  console.log('PASS: visible Spoolman IDs, numeric/hash search, linked sealed twins distinct, local twins grouped, exact local assignment, Canvas and desktop/mobile chooser')
} finally {
  try {
    const drained = page?.unrouteAll({ behavior: 'wait' })
    releaseCloseProbe?.()
    await drained
    if (closeProbeStarted) assert(closeProbeFinished, 'in-flight route finished before browser close')
  } finally {
    await browser?.close()
    await server.close()
  }
}
