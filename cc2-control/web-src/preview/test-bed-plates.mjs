import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Bed Levelling > Build Plates against the preview backend: measurements at several bed temperatures,
// mounting with and without a restart (also with a chosen measurement), the printer-screen note for the
// plate Z offset plus the installed nozzle's correction, editing the Z offset with the live adjustment,
// nozzles, saving, viewing and deleting; then the print dialog's choice of measurement and nozzle.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], posts = [], starts = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() !== 'POST' || !r.url().includes('/api/plates/')) return
    assert.equal(r.headers()['x-cc2-request'], '1', 'plate changes carry the mutation marker')
    posts.push([new URL(r.url()).pathname, r.postData() || ''])
  })
  await page.addInitScript(() => {
    const p = CanvasRenderingContext2D.prototype
    const begin = p.beginPath, close = p.closePath, fill = p.fill
    p.beginPath = function (...args) { this.__closed = false; return begin.apply(this, args) }
    p.closePath = function (...args) { this.__closed = true; return close.apply(this, args) }
    p.fill = function (...args) {
      if (this.__closed) this.canvas.__faces = (this.canvas.__faces || 0) + 1
      return fill.apply(this, args)
    }
    const descriptor = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, 'width')
    Object.defineProperty(HTMLCanvasElement.prototype, 'width', { ...descriptor, set(value) { this.__faces = 0; descriptor.set.call(this, value) } })
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  // A live session adjustment of -0.020 mm on top of the printer reference.
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.z_offset = { value: -0.075, pending: false, timed_out: false, reference: -0.055, adjustment: -0.02 }
    await route.fulfill({ response, json: data })
  })
  await page.goto(`${origin}/#bed/plates`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')])
  await page.getByRole('heading', { name: 'Build Plate Library' }).waitFor()
  const card = name => page.locator('[data-plate]').filter({ has: page.getByText(name, { exact: true }) })
  const row = (name, temp) => card(name).locator('[data-measure]').filter({ has: page.getByText(`${temp} °C`, { exact: true }) })
  const toast = text => page.getByText(text, { exact: true }).waitFor()
  const confirm = async text => {
    const dialog = page.getByRole('dialog')
    await dialog.getByText(text, { exact: false }).waitFor()
    await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  }
  await card('Smooth PEI').waitFor()
  assert.equal(await page.locator('[data-plate]').count(), 3)
  assert.ok(await page.getByRole('tab', { name: 'Build Plates' }).getAttribute('aria-selected'))
  await card('Textured PEI').getByText('In printer', { exact: true }).waitFor()
  assert.equal(await card('Cool Plate').getByText('In printer', { exact: true }).count(), 0)
  // Smooth PEI was calibrated at three bed temperatures; each row says where its mesh is.
  assert.equal(await card('Smooth PEI').locator('[data-measure]').count(), 3)
  await row('Smooth PEI', 60).getByText('Side mesh', { exact: true }).waitFor()
  await row('Smooth PEI', 80).getByText('Stored in printer', { exact: true }).waitFor()
  await row('Smooth PEI', 100).getByText('Library only', { exact: true }).waitFor()
  assert.match(await row('Smooth PEI', 60).innerText(), /0\.4 brass/)
  assert.match(await row('Smooth PEI', 100).innerText(), /Nozzle not recorded/)

  // In its slot already: mounts at once, no confirmation.
  await card('Textured PEI').getByRole('button', { name: 'Mount', exact: true }).click()
  await toast('Plate “Textured PEI” mounted.')
  assert.deepEqual(posts.at(-1), ['/api/plates/mount', '0f1e2d3c4b5a6978'])
  await card('Textured PEI').getByText('Mounted', { exact: true }).waitFor()
  // A mounted plate with a Z offset says that the printer screen would replace it.
  const note = page.getByText('The printer screen does not know this offset', { exact: false })
  await note.waitFor()
  assert.match(
    await note.textContent(),
    /replaces \+0\.010 mm\. Tune Z with “Live Z Offset” on the Control page instead, then add the adjustment to the plate with “Edit”\.$/,
  )

  // Not in its slot: the restart needs a confirmation, then the REBOOT line.
  await card('Cool Plate').getByRole('button', { name: 'Mount', exact: true }).click()
  await confirm('Write the mesh of “Cool Plate” to Side A and restart the printer?')
  await toast('The printer is restarting to load the plate mesh.')
  assert.deepEqual(posts.slice(-2), [['/api/plates/mount', '1234567890abcdef'], ['/api/plates/mount', '1234567890abcdef\nREBOOT']])
  await card('Cool Plate').getByText('In printer', { exact: true }).waitFor()
  await page.getByText('Mounted plate', { exact: true }).locator('..').getByText('Cool Plate', { exact: true }).waitFor()
  assert.equal(await card('Smooth PEI').getByText('In printer', { exact: true }).count(), 0)
  await note.waitFor({ state: 'detached' }) // Z 0 agrees with the screen's own zero
  // The side mesh it replaced keeps a printer profile.
  await row('Smooth PEI', 60).getByText('Stored in printer', { exact: true }).waitFor()

  // Edit the mounted plate: the live adjustment is added to the typed value.
  await card('Cool Plate').getByRole('button', { name: 'Edit', exact: true }).click()
  const z = card('Cool Plate').getByLabel('Z offset, mm')
  await z.fill('-0.015')
  await card('Cool Plate').getByRole('button', { name: 'Add the live adjustment (−0.020 mm)', exact: true }).click()
  assert.equal(await z.inputValue(), '-0.035')
  await card('Cool Plate').getByRole('button', { name: 'Save', exact: true }).click()
  await toast('Plate updated.')
  assert.deepEqual(posts.at(-1), ['/api/plates/edit', '1234567890abcdef\nCool Plate\n-0.035'])
  await card('Cool Plate').getByText('−0.035 mm', { exact: true }).waitFor()
  await note.waitFor()
  assert.match(await note.textContent(), /replaces −0\.035 mm\./)

  // Nozzles: the installed one adds its Z correction to the mounted plate.
  const nozzle = name => page.locator('[data-nozzle]').filter({ has: page.getByText(name, { exact: true }) })
  await nozzle('0.4 brass').getByText('Installed', { exact: true }).waitFor()
  await nozzle('0.6 hardened').getByRole('button', { name: 'Mark installed', exact: true }).click()
  await nozzle('0.6 hardened').getByText('Installed', { exact: true }).waitFor()
  assert.deepEqual(posts.at(-1), ['/api/plates/nozzle/select', 'dd00000000000006'])
  await page.getByText('with “0.6 hardened” (+0.020 mm): −0.015 mm', { exact: false }).waitFor()
  await page.waitForFunction(() => /replaces −0\.015 mm\./.test(document.body.innerText))
  const nozzleForm = page.locator('[data-nozzle-form]')
  await nozzleForm.getByPlaceholder('e.g. 0.4 brass').fill('0.2 brass')
  await nozzleForm.locator('input[type=number]').nth(0).fill('0.2')
  await nozzleForm.locator('input[type=number]').nth(1).fill('0.6')
  await nozzleForm.getByRole('button', { name: 'Add Nozzle', exact: true }).click()
  await toast('Use a diameter from 0.10 to 2.00 mm and a Z correction from −0.500 to +0.500 mm.')
  await nozzleForm.locator('input[type=number]').nth(1).fill('0.01')
  await nozzleForm.getByRole('button', { name: 'Add Nozzle', exact: true }).click()
  await toast('Nozzle saved.')
  assert.deepEqual(posts.at(-1), ['/api/plates/nozzle', '\n0.2 brass\n0.20\n0.010'])
  await nozzle('0.2 brass').waitFor()

  // A measurement that is not the side mesh is written to the slot with a restart.
  await row('Smooth PEI', 80).getByRole('button', { name: 'Make side mesh', exact: true }).click()
  await confirm('Write the 80 °C mesh of “Smooth PEI” to Side A and restart the printer?')
  await toast('The printer is restarting to load the plate mesh.')
  assert.deepEqual(posts.slice(-2), [
    ['/api/plates/mount', 'a1b2c3d4e5f60718\naa00000000000080'],
    ['/api/plates/mount', 'a1b2c3d4e5f60718\naa00000000000080\nREBOOT'],
  ])
  await row('Smooth PEI', 80).getByText('Side mesh', { exact: true }).waitFor()
  await card('Smooth PEI').getByText('Mounted', { exact: true }).waitFor()
  // A measurement can be deleted; the last one of a plate cannot.
  await row('Smooth PEI', 100).getByRole('button', { name: 'Delete', exact: true }).click()
  await confirm('Delete the 100 °C measurement of “Smooth PEI”?')
  await toast('Measurement deleted.')
  assert.deepEqual(posts.at(-1), ['/api/plates/measure/delete', 'aa00000000000100'])
  await row('Smooth PEI', 100).waitFor({ state: 'detached' })
  assert.equal(await row('Cool Plate', 60).getByRole('button', { name: 'Delete', exact: true }).count(), 0)
  // What a measurement records can be corrected: an old one gets its nozzle and temperature.
  await row('Cool Plate', 60).getByRole('button', { name: 'Edit measurement', exact: true }).click()
  const editRow = card('Cool Plate').locator('[data-measure]').first()
  await editRow.locator('input[type=number]').fill('65')
  await editRow.locator('select').selectOption('dd00000000000006')
  await editRow.getByRole('button', { name: 'Save', exact: true }).click()
  await toast('Measurement updated.')
  assert.deepEqual(posts.at(-1), ['/api/plates/measure/edit', 'cc00000000000060|65|dd00000000000006'.split('|').join(String.fromCharCode(10))])
  await row('Cool Plate', 65).getByText('0.6 hardened', { exact: true }).waitFor()

  // A new plate from the Side B mesh, with its bed temperature and nozzle; a name the backend refuses never leaves the page.
  const form = page.locator('section').filter({ has: page.getByRole('heading', { name: 'Save Printer Mesh as a Plate' }) })
  // Labels wrap their controls, so the names include the current values: address the controls directly.
  const side = form.locator('select').first(), name = form.locator('input').first()
  const [zInput, temp] = [form.locator('input[type=number]').nth(0), form.locator('input[type=number]').nth(1)]
  await side.selectOption('B')
  await name.fill('Bad "quote"')
  const before = posts.length
  await form.getByRole('button', { name: 'Save Plate', exact: true }).click()
  await toast('Use a name of up to 64 bytes without quotes or backslashes.')
  assert.equal(posts.length, before)
  await name.fill('Glass')
  await zInput.fill('0.005')
  await temp.fill('35')
  await form.getByRole('button', { name: 'Save Plate', exact: true }).click()
  await toast('Use a whole bed temperature from 40 to 110 °C.')
  assert.equal(posts.length, before)
  await temp.fill('70')
  await form.getByRole('button', { name: 'Save Plate', exact: true }).click()
  await toast('Plate saved.')
  assert.deepEqual(posts.at(-1), ['/api/plates/save', 'B\nGlass\n0.005\n70\ndd00000000000006']) // the installed nozzle
  await row('Glass', 70).waitFor()

  // The measurement opens in the 3D viewer of the Mesh tab.
  await row('Glass', 70).getByRole('button', { name: 'View mesh', exact: true }).click()
  await page.waitForFunction(() => location.hash === '#bed')
  const profile = page.locator('main select').first()
  await page.waitForFunction(() => /^measure:[0-9a-f]{16}$/.test(document.querySelector('main select')?.value || ''))
  await page.locator('main p', { hasText: 'Glass · 70 °C' }).first().waitFor() // the statistics name the measurement
  await page.waitForFunction(() => document.querySelector('main canvas')?.__faces === 100)
  assert.ok((await profile.locator('optgroup').allTextContents()).length >= 4)

  await page.getByRole('tab', { name: 'Build Plates' }).click()
  await card('Glass').getByRole('button', { name: 'Delete', exact: true }).last().click()
  await confirm('Delete plate “Glass”?')
  await toast('Plate deleted.')
  await card('Glass').waitFor({ state: 'detached' })

  // While printing, the printer-changing actions wait for Idle.
  // The scenario change reloads the page; the old page still shows an idle printer until then.
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'printing')])
  await page.getByRole('heading', { name: 'Build Plate Library' }).waitFor()
  const mount = card('Cool Plate').getByRole('button', { name: 'Mount', exact: true }) // the scenario reset restores Smooth PEI
  for (let i = 0; i < 100 && !(await mount.isDisabled()); i++) await page.waitForTimeout(100)
  assert.ok(await mount.isDisabled())
  assert.equal(await mount.getAttribute('title'), 'Available when idle')
  assert.ok(await page.getByRole('button', { name: 'Save Plate', exact: true }).isDisabled())

  await page.setViewportSize({ width: 390, height: 844 })
  assert.ok(await card('Smooth PEI').isVisible())
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal scroll on a phone')

  // The print dialog: the mounted plate's measurement nearest to the file's bed temperature, the nozzle
  // matching the file's diameter, and neither when the print probes its own mesh.
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0], bed_temperature: 85, nozzle_diameter: 0.6 } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {}, default1: {} } } } } } }))
  await page.route('**/api/gcode-files/print', r => {
    starts.push(r.request().postData())
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(`${origin}/#files`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')])
  const print = async check => {
    await page.getByRole('button', { name: 'Print', exact: true }).first().click()
    const dialog = page.getByRole('dialog')
    await dialog.waitFor()
    await check(dialog)
    await dialog.getByRole('button', { name: 'Start print', exact: true }).click()
    await dialog.waitFor({ state: 'hidden' })
    return starts.at(-1).split('\n')
  }
  let lines = await print(async dialog => {
    const mesh = dialog.locator('label', { hasText: 'Mesh of “Smooth PEI”' }).locator('select')
    assert.equal(await mesh.inputValue(), 'aa00000000000080') // 80 °C is nearest to 85 °C
    assert.ok(await mesh.locator('option[value="aa00000000000100"]').evaluate(o => o.disabled), 'a library-only measurement cannot be loaded')
    await dialog.getByText('The file heats the bed to 85 °C.', { exact: true }).waitFor()
    const nozzles = dialog.locator('label', { hasText: 'Nozzle' }).locator('select')
    assert.equal(await nozzles.inputValue(), 'dd00000000000006') // the only 0.6 mm nozzle
    assert.equal(await dialog.getByText('The file was sliced for a 0.60 mm nozzle.', { exact: true }).count(), 0)
    await nozzles.selectOption('dd00000000000004')
    await dialog.getByText('The file was sliced for a 0.60 mm nozzle.', { exact: true }).waitFor()
    await mesh.selectOption('aa00000000000060')
  })
  assert.deepEqual(lines.slice(3), ['A', 'saved', '0', 'aa00000000000060', 'dd00000000000004'])
  lines = await print(async dialog => {
    await dialog.getByText('Side B', { exact: true }).click()
    await dialog.getByText('The mounted plate “Smooth PEI” is on Side A.', { exact: true }).waitFor()
  })
  assert.deepEqual(lines.slice(3, 7), ['B', 'saved', '0', ''])
  lines = await print(async dialog => {
    await dialog.getByRole('checkbox', { name: 'Calibrate bed before printing', exact: true }).check()
    assert.equal(await dialog.locator('label', { hasText: 'Mesh of' }).count(), 0)
  })
  assert.deepEqual(lines.slice(3, 7), ['A', 'calibrate', '0', ''])
  assert.deepEqual(errors, [])
  console.log('PASS: plate measurements by bed temperature, mount (in place, with restart, with a chosen measurement), screen Z note with the nozzle correction, Z edit, nozzles, save, view, delete, idle guard, phone width and the print dialog choice')
} finally { await browser?.close(); await server.close() }
