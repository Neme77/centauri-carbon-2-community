// Bed alignment, step 1 with "With mesh": the printer's active mesh (GET /api/mesh) as a basis next to the saved and the
// temporary meshes, for whoever has no plate library. Its plate, temperature and nozzle are not known: the bed temperature
// is chosen as without a mesh, a short note says so, and the warnings about plate, temperature, nozzle and age are left out. The
// plate shape is taken off with the same calculation as for a saved mesh, quick and in a session. An adaptive mesh that
// does not reach the screws is named as not comparable; no active mesh offers nothing; the same mesh as a saved one is
// offered only once, as the saved one; an empty library still offers it. All data is synthetic.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { ALIGN_DEFAULTS, analyseScrewProbes, meshAt } from '../src/lib/mesh-align.ts'
import { syntheticMesh } from './mesh-align-synthetic.mjs'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
const IDS = ['FL', 'FR', 'RR', 'RL']
// The active mesh: tilted and bowed, over the full bed; an adaptive one over the middle only.
const fullMesh = syntheticMesh({ cols: 7, rows: 7, min_x: 10, max_x: 245, min_y: 10, max_y: 245, tiltX: 0.15, tiltY: -0.1, shape: (u, v) => 0.08 * (u * u + v * v) })
const adaptiveMesh = syntheticMesh({ cols: 5, rows: 5, min_x: 80, max_x: 180, min_y: 80, max_y: 180, shape: (u, v) => 0.05 * u * v })
const meshReply = g =>
  g
    ? { result: { status: { bed_mesh: { profile_name: 'default', mesh_min: [g.min_x, g.min_y], mesh_max: [g.max_x, g.max_y], probed_matrix: g.points, profiles: {} } } } }
    : // No active mesh, as a CC2 reports it after its screw measurement unloaded it (measured 18.11., mesh-nachher.json).
      { result: { status: { bed_mesh: { mesh_max: [0.0, 0.0], mesh_min: [0.0, 0.0], probed_matrix: [], profile_name: '', profiles: {} } } } }
const output = z =>
  `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${Object.entries(z)
    .map(([k, v]) => {
      const s = ALIGN_DEFAULTS.screws.find(x => x.id === k)
      return `${`// probe at ${s.x.toFixed(3)},${s.y.toFixed(3)} is z=${v.toFixed(6)}\n`.repeat(3)}// Result is z=${v.toFixed(6)}\n`
    })
    .join('')}`
const um = text => Number(text.replace(/ /g, ' ').replace('Z height: ', '').replace('−', '-').replace('+', '').split(/\n|±/)[0].trim().replace(/ µm$/, ''))
let browser
const errors = []
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  page.on('pageerror', e => errors.push(e.message))
  const now = Math.floor(Date.now() / 1000)
  let active = fullMesh
  let meshReads = 0
  await page.route('**/api/mesh', route => {
    meshReads++
    return route.fulfill({ json: meshReply(active) })
  })
  let emptyLibrary = false
  await page.route('**/api/plates', async route => {
    if (route.request().method() !== 'GET') return route.fallback()
    const lib = await (await route.fetch()).json()
    for (const p of lib.plates) for (const m of p.measures) m.measured = now - 3600
    if (emptyLibrary) Object.assign(lib, { plates: [], current: '' })
    return route.fulfill({ json: lib })
  })
  let job = null
  let generation = 80
  await page.route('**/api/console', route =>
    route.request().method() === 'GET' && job
      ? route.fulfill({ json: { busy: false, completed: true, success: true, generation, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: job } })
      : route.fallback()
  )
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const library = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const card = page.locator('#screwFocus')
  const base = card.locator('[data-level-base]')
  const activeOption = base.locator('option[value="active"]')
  const note = card.locator('[data-level-active-note]')
  const startButton = card.locator('[data-level-start-button]')
  const dialog = page.getByRole('dialog')
  // Step 1 with the tick, "Quick" and the given choices, as this browser keeps them.
  const open = async saved => {
    await page.evaluate(s => {
      localStorage.setItem('cc2-level-base', JSON.stringify(s))
      localStorage.setItem('cc2-level-mesh', '1')
      localStorage.setItem('cc2-level-mode', 'quick')
    }, saved)
    const reads = meshReads
    await page.goto(`${origin}/#bed/align`)
    await page.reload()
    await page.locator('#cc2-preview-banner').waitFor()
    await card.waitFor()
    for (let i = 0; i < 50 && meshReads === reads; i++) await page.waitForTimeout(100)
    assert.ok(meshReads > reads, 'the tab reads the active mesh')
    const b = page.locator('[data-level-step="2"] > button')
    if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
  }
  const custom60 = { choice: 'custom', temp: '60', plain: 'custom', plainTemp: '60' }
  // Probes that carry the plate shape of the active mesh plus a known tilt.
  const probes = (grid, tiltX, tiltY) =>
    Object.fromEntries(ALIGN_DEFAULTS.screws.map(s => [s.id, 1.5 + meshAt(grid, s.x, s.y) + (tiltX * s.x + tiltY * s.y) / ALIGN_DEFAULTS.tilt_span_mm]))
  const shapeShown = async (grid, z, what) => {
    await card.locator('[data-screw-shape="FR"]').waitFor({ timeout: 10000 })
    const fit = analyseScrewProbes(z, grid, ALIGN_DEFAULTS)
    assert.ok(fit, `${what}: the active mesh can be compared`)
    for (const [i, id] of IDS.entries()) {
      const got = um(await card.locator(`[data-screw-shape="${id}"]`).innerText())
      const want = Math.round((fit.shape[i] - fit.shape[0]) * 1000)
      assert.ok(Math.abs(got - want) <= 1, `${what}: plate shape ${id} ${got} vs ${want}`)
    }
  }

  // (1) The active mesh is offered (first entry, the project's word for it), chosen, and its temperature is the one
  // without a mesh; the note says what is not known; the evaluation takes its plate shape off.
  let z = probes(fullMesh, 0.2, -0.12)
  job = output(z)
  await open(custom60)
  await activeOption.waitFor({ state: 'attached' })
  assert.equal(await activeOption.innerText(), 'Active mesh on the printer')
  assert.equal(await base.locator('option').first().getAttribute('value'), 'active', 'the active mesh is the first entry')
  assert.equal(await note.count(), 0, 'no note before it is chosen')
  await base.selectOption('active')
  await note.getByText('Active mesh: choose the bed temperature below.', { exact: true }).waitFor()
  assert.equal(await card.locator('[data-level-active-partial]').count(), 0, 'a full mesh reaches the screws')
  const temp = card.locator('[data-level-active-temp]')
  assert.equal(await temp.inputValue(), 'custom', 'the temperature comes from the choice without a mesh')
  await card.locator('[data-level-custom-temp]').fill('65')
  await startButton.getByText('Evaluated with the active mesh', { exact: false }).waitFor()
  await shapeShown(fullMesh, z, 'quick')
  assert.equal(await card.locator('[data-level-mesh-warning]').filter({ hasText: /measured at|belongs to plate|nozzle/ }).count(), 0, 'no warnings about plate, temperature or nozzle')
  assert.equal(await card.locator('[data-level-age], [data-level-saved-mesh-aged], [data-level-work-note]').count(), 0, 'no age note')
  assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem('cc2-level-base'))), { choice: 'active', temp: '60', plain: 'custom', plainTemp: '65' })
  // The start asks with that temperature.
  await startButton.click()
  await dialog.locator('[data-confirm-text]').getByText(/heat the bed to 65 °C/).waitFor()
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  // A material as the temperature of the active mesh.
  const material = await temp.locator('option[value^="material:"]').first().getAttribute('value')
  if (material) {
    await temp.selectOption(material)
    assert.equal(await card.locator('[data-level-custom-temp]').count(), 0)
    assert.equal(await base.inputValue(), 'active', 'the mesh stays chosen')
    await temp.selectOption('custom')
  }
  // Without the tick the temperature choice is the same one; with it the active mesh comes back.
  await card.locator('[data-level-mesh-toggle] input').click()
  assert.equal(await base.inputValue(), 'custom')
  assert.equal(await card.locator('[data-screw-shape]').count(), 0, 'no plate shape without the tick')
  await card.locator('[data-level-mesh-toggle] input').click()
  assert.equal(await base.inputValue(), 'active')
  await shapeShown(fullMesh, z, 'quick, tick again')

  // (2) An adaptive mesh of a print that does not reach the screws: named in step 1, not compared in step 2.
  active = adaptiveMesh
  await open({ ...custom60, choice: 'active' })
  await note.locator('[data-level-active-partial]').getByText('This mesh cannot be compared', { exact: false }).waitFor()
  await card.locator('[data-level-mesh-unusable]').waitFor()
  assert.equal(await card.locator('[data-screw-shape]').count(), 0, 'nothing is calculated from outside the mesh')

  // (3) No active mesh: not offered; a kept choice of it falls back to a temperature.
  active = null
  await open({ ...custom60, choice: 'active' })
  await page.waitForFunction(() => JSON.parse(localStorage.getItem('cc2-level-base')).choice !== 'active')
  assert.equal(await activeOption.count(), 0, 'no entry without an active mesh')
  assert.equal(await note.count(), 0)

  // (4) The active mesh is a saved mesh of the library: offered once, as the saved mesh (with what it knows).
  const mounted = library.plates.find(p => p.id === library.current) ?? library.plates[0]
  const saved = mounted.measures[0]
  active = saved.mesh
  await open({ ...custom60, choice: 'active' })
  await page.waitForFunction(id => JSON.parse(localStorage.getItem('cc2-level-base')).choice === `measure:${id}`, saved.id)
  assert.equal(await base.inputValue(), `measure:${saved.id}`)
  assert.equal(await activeOption.count(), 0, 'no second entry for the same mesh')

  // (5) An empty plate library: the active mesh can still be chosen and the way's start is there.
  emptyLibrary = true
  active = fullMesh
  await open(custom60)
  assert.equal(await base.locator('optgroup[label="Saved meshes"]').count(), 0)
  await base.selectOption('active')
  await startButton.waitFor()
  assert.equal(await startButton.isDisabled(), false, 'the start works without a plate')
  await shapeShown(fullMesh, z, 'empty library')
  emptyLibrary = false

  // (6) Quick: the firmware unloads its active mesh during the screw measurement (measured on a CC2, 18.11.: from the
  // preparation on /api/mesh reports an empty profile_name and probed_matrix). The copy taken at the start stays the basis,
  // also after a reload; the note does not mention the copy.
  const eventually = async (check, what) => {
    let last
    for (let i = 0; i < 50; i++) {
      try {
        return await check()
      } catch (e) {
        last = e
        await page.waitForTimeout(200)
      }
    }
    throw new Error(`${what}: ${last?.message}`)
  }
  let measured = null
  await page.route('**/api/control', route => {
    const body = route.request().postData() || ''
    if (route.request().method() !== 'POST' || !/^(screws:soak:\d+:)?screws:measure/.test(body)) return route.fallback()
    active = null // unloaded by the firmware
    generation += 1000
    job = output(measured)
    return route.fulfill({ json: { accepted: true } })
  })
  active = fullMesh
  job = null
  await open({ ...custom60, choice: 'active' })
  measured = probes(fullMesh, -0.1, 0.25)
  await startButton.getByText('Measure only (quick)').waitFor()
  await startButton.click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await eventually(() => shapeShown(fullMesh, measured, 'quick, mesh unloaded'), 'quick, mesh unloaded')
  // A reload reads the printer again, which reports no mesh now (as on the CC2).
  await open({ ...custom60, choice: 'active' })
  // The device's symptom was an evaluation without the plate shape: it has to be there with the copy.
  await shapeShown(fullMesh, measured, 'after a reload, the printer without a mesh')
  // Time for the answer and the library to arrive (a choice without a mesh falls back once both are there).
  await page.waitForTimeout(2500)
  assert.equal(await base.inputValue(), 'active', 'the choice stays with the copy (does not fall back to a temperature)')
  assert.ok(await page.evaluate(() => localStorage.getItem('cc2-level-active')), 'the copy is kept in this browser')
  assert.equal(await note.innerText(), 'Active mesh: choose the bed temperature below.', 'the note stays the short sentence with the copy')
  await shapeShown(fullMesh, measured, 'after a reload')
  // Another active mesh later: the evaluated measurement keeps the copy; the next measurement takes the mesh applied then.
  const otherMesh = syntheticMesh({ cols: 9, rows: 9, min_x: 6, max_x: 246, min_y: 6, max_y: 246, tiltX: -0.05, shape: (u, v) => -0.06 * u * u })
  active = otherMesh
  await open({ ...custom60, choice: 'active' })
  await shapeShown(fullMesh, measured, 'another mesh applied, old measurement')
  measured = probes(otherMesh, 0.1, 0.1)
  await startButton.click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await eventually(() => shapeShown(otherMesh, measured, 'new measurement, new mesh'), 'new measurement, new mesh')
  await page.unroute('**/api/control')

  // (7) With session: the session's heights are evaluated with the active mesh too, from the copy of its start, also
  // after a reload while the session runs and the printer reports no mesh.
  job = null
  active = fullMesh
  await open({ ...custom60, choice: 'active' })
  await card.locator('[data-level-start-mode-button="session"]').click()
  await card.locator('[data-level-soak]').selectOption('0')
  await startButton.getByText('Start session').waitFor()
  await startButton.getByText('Evaluated with the active mesh', { exact: false }).waitFor()
  await startButton.click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.locator('[data-level-session-bar]').waitFor({ timeout: 10000 })
  await card.locator('[data-screw-zheight="RL"]').waitFor({ timeout: 15000 })
  const zs = {}
  for (const id of IDS) zs[id] = um(await card.locator(`[data-screw-zheight="${id}"]`).innerText()) / 1000
  await shapeShown(fullMesh, zs, 'session')
  active = null // unloaded by the firmware
  await open({ ...custom60, choice: 'active' })
  await page.locator('[data-level-session-bar]').waitFor({ timeout: 10000 })
  assert.equal(await base.inputValue(), 'active', 'the choice stays in the session')
  await eventually(async () => {
    const z2 = {}
    for (const id of IDS) z2[id] = um(await card.locator(`[data-screw-zheight="${id}"]`).innerText()) / 1000
    await shapeShown(fullMesh, z2, 'session after a reload')
  }, 'session after a reload')

  assert.deepEqual(errors, [])
  console.log('PASS bed alignment with the active mesh: offered, temperature as without a mesh, no plate warnings, adaptive, none, same as saved, empty library, copy at the start (unloaded mesh, reload, another mesh), session')
} finally {
  await browser?.close()
  await server.close()
}
