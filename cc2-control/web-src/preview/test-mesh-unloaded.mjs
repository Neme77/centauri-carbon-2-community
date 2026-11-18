// Mesh & Saved Profiles, profile "Active mesh": a printer that answers but has no mesh loaded (its nozzle cleaning G180
// unloads the mesh, so after every screw measurement, until a print loads its side's mesh again; measured on a CC2,
// 18.11.: empty profile_name and probed_matrix, mesh_min/mesh_max 0, the stored profiles stay) is told as such in the note
// at the top and in the empty area, instead of waiting. Without an answer the page still waits. All data is synthetic.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
const HINT = 'The printer has no mesh loaded right now. Its nozzle cleaning unloads the mesh; the next print loads the mesh of its plate side again.'
const grid = Array.from({ length: 11 }, (_, y) => Array.from({ length: 11 }, (_, x) => Math.round((x - y) * 0.01 * 1e6) / 1e6))
const profiles = { default: { mesh_params: { min_x: 6, max_x: 246, min_y: 6, max_y: 246, x_count: 11, y_count: 11 }, points: grid } }
const replies = {
  // As the CC2 answers after a screw measurement (the stored profiles are still there).
  unloaded: { id: 201, result: { status: { bed_mesh: { mesh_max: [0.0, 0.0], mesh_min: [0.0, 0.0], probed_matrix: [], profile_name: '', profiles } } } },
  loaded: { id: 201, result: { status: { bed_mesh: { mesh_max: [246, 246], mesh_min: [6, 6], probed_matrix: grid, profile_name: 'default', profiles } } } },
}
let browser
const errors = []
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  let mode = 'unloaded'
  let reads = 0
  await page.route('**/api/mesh', route => {
    reads++
    // No answer: the request fails, as when the printer cannot be reached.
    return mode === 'none' ? route.abort('failed') : route.fulfill({ json: replies[mode] })
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const note = page.locator('[data-mesh-note]')
  const area = page.locator('[data-mesh-unloaded]')
  const open = async m => {
    mode = m
    const before = reads
    await page.goto(`${origin}/#bed`)
    await page.reload()
    await page.locator('#cc2-preview-banner').waitFor()
    await note.waitFor()
    for (let i = 0; i < 50 && reads === before; i++) await page.waitForTimeout(100)
    assert.ok(reads > before, `${m}: the mesh is read`)
    await page.waitForTimeout(500)
  }
  const values = page.getByRole('button', { name: 'Values', exact: true })
  const view3d = page.getByRole('button', { name: 'View 3D', exact: true })

  // (1) The printer answers without a mesh: the hint in the note and in the empty area, no waiting text.
  await open('unloaded')
  await note.getByText(HINT, { exact: true }).waitFor()
  await area.getByText(HINT, { exact: true }).waitFor()
  assert.equal(await page.locator('main').getByText(/Waiting for (the printer mesh|live mesh data)/).count(), 0, 'no waiting text')
  await values.click()
  await area.getByText(HINT, { exact: true }).waitFor()
  await view3d.click()
  // "Load Current Mesh" only reads again: still no mesh, still the hint.
  const reload = page.getByRole('button', { name: 'Load Current Mesh', exact: true })
  if (await reload.count()) {
    await reload.click()
    await note.getByText(HINT, { exact: true }).waitFor()
  }
  // A saved profile still shows its points.
  await page.locator('main select').first().selectOption('default')
  assert.equal(await area.count(), 0, 'a saved profile has its points')
  await page.locator('main select').first().selectOption('active')
  await note.getByText(HINT, { exact: true }).waitFor()

  // (2) No answer: the page keeps waiting, without the hint.
  await open('none')
  await note.getByText('Waiting for the printer mesh.', { exact: true }).waitFor()
  assert.equal(await area.count(), 0, 'no hint without an answer')
  assert.equal(await page.getByText(HINT).count(), 0)

  // (3) A loaded mesh: neither waiting nor the hint.
  await open('loaded')
  await note.getByText('Active mesh loaded', { exact: false }).waitFor()
  assert.equal(await page.getByText(HINT).count(), 0)

  assert.deepEqual(errors, [])
  console.log('PASS Mesh tab: a printer without a loaded mesh is told so; without an answer the page waits; a loaded mesh shows')
} finally {
  await browser?.close()
  await server.close()
}
