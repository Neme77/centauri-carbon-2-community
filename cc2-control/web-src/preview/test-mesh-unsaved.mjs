// A mesh calibration on a plate that was measured but could not be saved to the plate (result "failed", error "saving"):
// the field at the top of every page keeps an amber note with the reason and a button that stores the mesh in the plate
// (POST /api/plates/measure with plate, temperature and nozzle of that run). It is not told twice as a toast. A refusal
// keeps the note and shows the reason; success reads the library, takes the new measurement as the basis and removes
// the note; "Dismiss" removes it for good; a new calibration removes it; a working-mesh run never has the button.
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
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH
      ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] }
      : process.env.CC2_BROWSER_EXECUTABLE
        ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE }
        : {}),
  })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  const measureCalls = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() === 'POST' && new URL(r.url()).pathname === '/api/plates/measure') measureCalls.push(r.postData())
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  const PLATE = 'a1b2c3d4e5f60718'
  const NOZZLE = 'dd00000000000004'
  const call = (url, body = '') =>
    page.evaluate(([u, b]) => fetch(u, { method: 'POST', headers: { 'X-CC2-Request': '1' }, body: b }).then(r => r.status), [url, body])
  const step = (body = '') => call('/__preview/calibration-step', body)
  // A run on the plate, moved on to the saving stage and then failed there.
  const failSaving = async (temp, work = false) => {
    assert.equal(await call('/api/plates/calibrate', `A\n${temp}\n0\n${NOZZLE}\n${PLATE}${work ? '\nwork' : ''}`), 202)
    for (let i = 0; i < 4; i++) await step() // homing, heating, soaking, probing -> saving
    await page.locator('[data-calibration-bar="saving"]').first().waitFor({ timeout: 25000 }) // the page sees the run end, as on the printer
    await step('fail')
  }
  const note = page.locator('[data-mesh-unsaved]')
  const library = () => page.evaluate(async () => (await fetch('/api/plates')).json())

  await page.goto(`${origin}/#bed`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  assert.equal(await note.count(), 0, 'no note without a failed run')

  // The run fails while saving: the note stays, on this page and on every other, with the reason and both buttons.
  await failSaving(75)
  await note.waitFor({ timeout: 25000 })
  await note.getByText('The calibration finished, but saving it to the plate failed: Cannot save the plate library', { exact: true }).waitFor()
  assert.equal(await page.getByText(/saving it to the plate failed/).count(), 1, 'told once: no toast beside the note')
  await note.getByRole('button', { name: 'Save the mesh to the plate', exact: true }).waitFor()
  await note.getByRole('button', { name: 'Dismiss', exact: true }).waitFor()
  await page.goto(`${origin}/#dashboard`)
  await note.waitFor()
  await page.goto(`${origin}/#bed/align`)
  await note.waitFor()

  // A refusal: the reason is shown, the note stays, nothing is stored.
  await page.route('**/api/plates/measure', route => route.fulfill({ status: 409, json: { ok: false, error: 'The plate library has no room for another measurement' } }))
  await note.getByRole('button', { name: 'Save the mesh to the plate', exact: true }).click()
  await note.locator('[data-mesh-unsaved-error]').getByText('The plate library has no room for another measurement', { exact: false }).waitFor()
  assert.deepEqual(measureCalls, [`${PLATE}\n75\n${NOZZLE}`], 'the request carries plate, temperature and nozzle of the failed run')
  assert.equal((await library()).plates.find(p => p.id === PLATE).measures.some(m => m.temp === 75), false)
  await page.unroute('**/api/plates/measure')

  // Success: the measurement is in the library, it is the basis, the note is gone, a short message tells it.
  await note.getByRole('button', { name: 'Save the mesh to the plate', exact: true }).click()
  await page.getByText('Saved to “Smooth PEI” as its 75 °C measurement.', { exact: true }).waitFor()
  await note.waitFor({ state: 'detached', timeout: 25000 })
  assert.deepEqual(measureCalls, [`${PLATE}\n75\n${NOZZLE}`, `${PLATE}\n75\n${NOZZLE}`])
  const saved = (await library()).plates.find(p => p.id === PLATE).measures.find(m => m.temp === 75 && m.nozzle === NOZZLE)
  assert.ok(saved, 'the new measurement is in the library')
  assert.match(await page.evaluate(() => localStorage.getItem('cc2-level-base')), new RegExp(saved.id), 'the new measurement is the basis')
  await page.reload()
  await page.locator('#cc2-preview-banner').waitFor()
  await new Promise(r => setTimeout(r, 1500))
  assert.equal(await note.count(), 0, 'the stored measurement keeps the note away after a reload')

  // A new calibration removes the note; "Dismiss" removes it for good.
  await failSaving(76)
  await note.waitFor({ timeout: 25000 })
  await call('/api/plates/calibrate', `A\n77\n0\n${NOZZLE}\n${PLATE}`)
  await note.waitFor({ state: 'detached', timeout: 25000 })
  for (let i = 0; i < 3; i++) await step()
  await step('fail') // stops in the probing stage: not a saving failure
  await failSaving(76)
  await note.waitFor({ timeout: 25000 })
  await note.getByRole('button', { name: 'Dismiss', exact: true }).click()
  await note.waitFor({ state: 'detached' })
  await page.reload()
  await page.locator('#cc2-preview-banner').waitFor()
  await new Promise(r => setTimeout(r, 1500))
  assert.equal(await note.count(), 0, 'a dismissed note does not come back after a reload')

  // A run that keeps its mesh only as the working mesh has no button; its failure is told as before.
  await failSaving(78, true)
  await page.getByText('The calibration finished, but the mesh could not be read', { exact: false }).waitFor({ timeout: 25000 })
  assert.equal(await note.count(), 0, 'no note for a working mesh')
  assert.deepEqual(errors, [])
  console.log('PASS: unsaved mesh note: reason, request, refusal, success, dismiss, new calibration, working mesh')
} catch (error) {
  console.error(error)
  process.exitCode = 1
} finally {
  await browser?.close()
  await server.close()
}
