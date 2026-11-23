import assert from 'node:assert/strict'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Assigning a spool to a Canvas tray: whether the page also writes the spool's filament to the printer, and why not.
// A tray that reports "filament there, type and colour empty" (the printer's exclamation mark) is written to; every
// other case in which nothing is written says so in one sentence. "Does the question need asking" stays as it was.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.setDefaultTimeout(10000)
  const errors = []
  const controls = []
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/control', r => {
    controls.push(r.request().postData())
    return r.fulfill({ json: { accepted: true } })
  })
  const reports = {}
  await page.route('**/api/spools', async r => {
    const response = await r.fetch()
    const json = await response.json()
    for (const [n, printer] of Object.entries(reports)) json.slots[n].printer = printer
    await r.fulfill({ response, json })
  })
  const post = (route, fields) =>
    page.evaluate(
      ([route, body]) => fetch(route, { method: 'POST', headers: { 'X-CC2-Request': '1' }, body }).then(r => r.json()),
      [route, Object.entries(fields).map(([k, v]) => `${k}=${v}`).join('\n')]
    )
  const save = fields =>
    post('/api/spools/save', { net: 1000, remaining: 1000, tare: 150, low: 100, diameter: 1.75, density: 1.24, ...fields })

  await page.goto(`${origin}/#spools`)
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('button', { name: 'Turn on spool tracking' }).click()
  await page.getByText('Spool tracking is on.', { exact: true }).waitFor()

  const redFields = { name: 'Red PLA test', brand: 'ELEGOO', material: 'PLA', color: '#EF5350' }
  const silkFields = { name: 'Gold Silk test', brand: 'ELEGOO', material: 'Silk PLA', color: '#C7A44A' }
  const red = (await save(redFields)).id
  const silk = (await save(silkFields)).id
  const dialog = page.getByRole('dialog')
  const write = dialog.getByLabel("Also set Slot 4 on the printer to this spool's material and colour")
  const reason = text => dialog.getByText(text, { exact: true })
  const FITS = 'What the printer reports for this slot already matches this spool.'
  const DIFFERS = 'The printer reports a different material or colour for this slot.'
  const card = page.locator('[data-slot="3"]')
  const NONE = ['Not written to the printer:', 'What the printer reports']
  // Opens the chooser for the fourth tray with its spool already assigned and the tray reporting `report`.
  const open = async (spool, label, report, scene = 'idle') => {
    reports[3] = report
    if (scene === 'idle') await post('/api/spools/assign', { slot: 3, spool })
    // A reload, not a goto: the address differs only by its hash, which would keep the stale page and its trays.
    await page.reload()
    // Another scene starts the preview backend afresh: tracking is off again, the inventory may be too.
    if (scene !== 'idle') {
      await page.selectOption('#preview-scene', scene)
      await page.getByRole('button', { name: 'Turn on spool tracking' }).click()
      await page.getByText('Spool tracking is on.', { exact: true }).waitFor()
    }
    // Resave under the new backend: the id of the first save may be gone.
    const id = scene === 'idle' ? spool : (await save(spool === red ? redFields : silkFields)).id
    if (scene !== 'idle') await post('/api/spools/assign', { slot: 3, spool: id })
    // The tray card names its spool once the page has read the inventory; only then is the chooser's list complete.
    await page.locator('[data-slot="3"]').getByText(label).first().waitFor({ timeout: 20000 })
    await page.locator('[data-slot="3"]').getByRole('button', { name: 'Choose spool' }).click()
    await dialog.waitFor()
    await dialog.getByRole('radio', { name: label }).check()
  }
  const close = async () => {
    await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
    await dialog.waitFor({ state: 'detached' })
  }
  const noSentence = async () => {
    for (const start of NONE) assert.equal(await dialog.getByText(start, { exact: false }).count(), 0, `no sentence “${start}”`)
  }

  // 1. Filament there, type and colour empty (the printer's exclamation mark): it is written, and the box says so.
  await open(red, /Red PLA test/, { status: 1, type: '', name: '', brand: '', color: '', code: '' })
  await write.waitFor()
  assert.ok(await write.isChecked())
  await noSentence()
  // "Nothing reported" still counts as matching for the tray card: no "differs" hint (fits() is unchanged).
  assert.equal(await card.getByText(DIFFERS).count(), 0)
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  assert.deepEqual(controls, ['canvas:material:3:PLA:EF5350:190:230:PLA:ELEGOO'])

  // 2. The tray already reports this spool: nothing to write, said in one sentence.
  controls.length = 0
  await open(red, /Red PLA test/, { status: 1, type: 'PLA', name: 'PLA', brand: 'ELEGOO', color: '#EF5350', code: '' })
  await reason(FITS).waitFor()
  // The sentence is a live status message for screen readers.
  await dialog.getByRole('status').filter({ hasText: FITS }).waitFor()
  assert.equal(await write.count(), 0)
  await close()

  // 2b. A tray that reports only one of the two is not "nothing": what it does report matches, so nothing is written.
  for (const half of [
    { type: '', name: '', color: '#EF5350' },
    { type: 'PLA', name: 'PLA', color: '' },
  ]) {
    await open(red, /Red PLA test/, { status: 1, brand: '', code: '', ...half })
    await reason(FITS).waitFor()
    assert.equal(await write.count(), 0)
    await close()
  }

  // 3. Another filament in the tray: written, no sentence (unchanged).
  await open(red, /Red PLA test/, { status: 1, type: 'PETG', name: 'PETG', brand: 'ELEGOO', color: '#16191D', code: '' })
  await write.waitFor()
  await noSentence()
  assert.equal(await card.getByText(DIFFERS).count(), 1)
  await close()

  // (A tray without filament cannot be chosen from the page; the question that opens for inserted filament writes to it,
  // see test-spools.mjs.)

  // 4. A material without a preset stays without one; the reason is shown instead of silence.
  await open(silk, /Gold Silk test/, { status: 1, type: 'PETG', name: 'PETG', brand: 'ELEGOO', color: '#16191D', code: '' })
  await reason('Not written to the printer: no material setting matches this spool.').waitFor()
  assert.equal(await write.count(), 0)
  await close()

  // 5. The printer is busy: the reason is shown (also for an empty tray, which would otherwise be written to).
  controls.length = 0
  await open(red, /Red PLA test/, { status: 1, type: 'PETG', name: 'PETG', brand: 'ELEGOO', color: '#16191D', code: '' }, 'printing')
  await reason('Not written to the printer while it is busy.').waitFor()
  assert.equal(await write.count(), 0)
  await close()
  await open(red, /Red PLA test/, { status: 1, type: '', name: '', brand: '', color: '', code: '' }, 'printing')
  await reason('Not written to the printer while it is busy.').waitFor()
  assert.equal(await write.count(), 0)
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  assert.deepEqual(controls, [], 'nothing is written while the printer is busy')

  assert.deepEqual(errors, [])
  console.log('PASS: an empty tray is written to, and every case without a write names its reason')
} finally {
  await browser?.close()
  await server.close()
}
