import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Bed Levelling > Build Plates against the preview backend: mounting with and without a restart,
// the printer-screen note for a plate Z offset, editing the Z offset with the live adjustment,
// saving, viewing and deleting a plate.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], posts = []
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
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('heading', { name: 'Build Plate Library' }).waitFor()
  const card = name => page.locator('[data-plate]').filter({ has: page.getByText(name, { exact: true }) })
  const toast = text => page.getByText(text, { exact: true }).waitFor()
  await card('Smooth PEI').waitFor()
  assert.equal(await page.locator('[data-plate]').count(), 3)
  assert.ok(await page.getByRole('tab', { name: 'Build Plates' }).getAttribute('aria-selected'))
  await card('Textured PEI').getByText('In printer', { exact: true }).waitFor()
  assert.equal(await card('Cool Plate').getByText('In printer', { exact: true }).count(), 0)

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
  const dialog = page.getByRole('dialog')
  await dialog.getByText('Write the mesh of “Cool Plate” to Side A and restart the printer?', { exact: false }).waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await toast('The printer is restarting to load the plate mesh.')
  assert.deepEqual(posts.slice(-2), [['/api/plates/mount', '1234567890abcdef'], ['/api/plates/mount', '1234567890abcdef\nREBOOT']])
  await card('Cool Plate').getByText('In printer', { exact: true }).waitFor()
  await page.getByText('Mounted plate', { exact: true }).locator('..').getByText('Cool Plate', { exact: true }).waitFor()
  assert.equal(await card('Smooth PEI').getByText('In printer', { exact: true }).count(), 0)
  await note.waitFor({ state: 'detached' }) // Z 0 agrees with the screen's own zero

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

  // A new plate from the Side B mesh; a name the backend refuses never leaves the page.
  const form = page.locator('section').filter({ has: page.getByRole('heading', { name: 'Save Printer Mesh as a Plate' }) })
  // Labels wrap their controls, so the names include the current values: address the controls directly.
  const [side, name, zInput] = [form.locator('select'), form.locator('input').first(), form.locator('input[type=number]')]
  await side.selectOption('B')
  await name.fill('Bad "quote"')
  const before = posts.length
  await form.getByRole('button', { name: 'Save Plate', exact: true }).click()
  await toast('Use a name of up to 64 bytes without quotes or backslashes.')
  assert.equal(posts.length, before)
  await name.fill('Glass')
  await zInput.fill('0.005')
  await form.getByRole('button', { name: 'Save Plate', exact: true }).click()
  await toast('Plate saved.')
  assert.deepEqual(posts.at(-1), ['/api/plates/save', 'B\nGlass\n0.005'])
  await card('Glass').waitFor()

  // The plate mesh opens in the 3D viewer of the Mesh tab.
  await card('Glass').getByRole('button', { name: 'View mesh', exact: true }).click()
  await page.waitForFunction(() => location.hash === '#bed')
  const profile = page.locator('main select').first()
  assert.match(await profile.inputValue(), /^plate:[0-9a-f]{16}$/)
  await page.locator('main p', { hasText: 'Plate · Glass' }).first().waitFor() // the statistics name the plate
  await page.waitForFunction(() => document.querySelector('main canvas')?.__faces === 100)

  await page.getByRole('tab', { name: 'Build Plates' }).click()
  await card('Glass').getByRole('button', { name: 'Delete', exact: true }).click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await toast('Plate deleted.')
  await card('Glass').waitFor({ state: 'detached' })

  // While printing, the printer-changing actions wait for Idle.
  await page.selectOption('#preview-scene', 'printing')
  await page.getByRole('heading', { name: 'Build Plate Library' }).waitFor()
  const mount = card('Cool Plate').getByRole('button', { name: 'Mount', exact: true }) // the scenario reset restores Smooth PEI
  assert.ok(await mount.isDisabled())
  assert.equal(await mount.getAttribute('title'), 'Available when idle')
  assert.ok(await page.getByRole('button', { name: 'Save Plate', exact: true }).isDisabled())

  await page.setViewportSize({ width: 390, height: 844 })
  assert.ok(await card('Smooth PEI').isVisible())
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'no horizontal scroll on a phone')
  assert.deepEqual(errors, [])
  console.log('PASS: plate library mount (in place and with restart), screen Z note, Z edit with live adjustment, save, view, delete, idle guard and phone width')
} finally { await browser?.close(); await server.close() }
