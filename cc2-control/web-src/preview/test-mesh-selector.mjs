import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir } from 'node:fs/promises'
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const posts = [], errors = []
  page.on('pageerror', e => errors.push(e.message))
  let current = '', active = 'default', pending = '', idle = true, fail = false
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async r => {
    const response = await r.fetch(), data = await response.json()
    data.machine.status = idle ? 1 : 2
    data.machine.status_name = idle ? 'Idle' : 'Printing'
    data.print.enabled = !idle
    data.print.state = idle ? 'standby' : 'printing'
    data.connected = true; data.last_message_age = 0
    await r.fulfill({ response, json: data })
  })
  await page.route('**/api/plates', async r => {
    const response = await r.fetch(), data = await response.json()
    data.current = current; data.pending = pending; data.mesh_profile = active
    await r.fulfill({ response, json: data })
  })
  await page.route('**/api/plates/mount', r => {
    const body = r.request().postData()
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    posts.push(body)
    if (fail) { current = ''; return r.fulfill({ status: 503, json: { error: 'Cannot verify the active plate mesh' } }) }
    if (body.includes('aa00000000000080')) {
      if (!body.includes('REBOOT')) return r.fulfill({ status: 409, json: { reboot_required: true, error: 'Writing this mesh to the printer needs a printer restart' } })
      current = pending = body.split('\n')[0]
      return r.fulfill({ status: 202, json: { mounted: true, reboot: true } })
    }
    current = body.split('\n')[0]; active = current === '0f1e2d3c4b5a6978' ? 'default1' : 'default'
    return r.fulfill({ json: { mounted: true, reboot: false } })
  })
  await page.goto(server.resolvedUrls.local[0] + '#bed/plates')
  const box = page.locator('[data-mesh-selector]')
  const selector = box.getByRole('combobox', { name: 'Library mesh', exact: true })
  const apply = box.getByRole('button', { name: 'Mount and activate', exact: true })
  await box.waitFor()
  assert(await apply.isDisabled())
  await selector.selectOption('bb00000000000060')
  assert.equal(posts.length, 0, 'selecting a saved mesh must not issue a printer command')
  assert.match(await box.innerText(), /Active mesh on the printer: Side A/)
  await apply.click()
  await page.getByRole('dialog').getByText('Confirm the installed plate:', { exact: false }).waitFor()
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(posts.length, 0)
  await apply.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await box.getByText('Side B', { exact: true }).waitFor()
  assert.equal(posts.at(-1), '0f1e2d3c4b5a6978\nbb00000000000060')
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'page overflow ' + width)
    const shots = process.env.CC2_MESH_SCREENSHOTS
    if (shots) { await mkdir(shots, { recursive: true }); await page.screenshot({ path: path.join(shots, 'mesh-selector-' + width + '.png') }) }
  }
  // An unverified activation reports the refusal; it cannot become a successful mount.
  fail = true
  await selector.selectOption('aa00000000000060')
  await apply.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByRole('alert').getByText('Cannot verify the active plate mesh', { exact: false }).waitFor()
  fail = false
  await selector.selectOption('aa00000000000080')
  await apply.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert(!posts.some(body => body.includes('REBOOT')), 'restart must require its own confirmation')
  await apply.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByRole('dialog').getByText('Write the 80 °C mesh', { exact: false }).waitFor()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('CC2 Control checks it once the printer is back.', { exact: false }).waitFor()
  assert(posts.at(-1).endsWith('REBOOT'))
  assert.equal(await page.locator('[data-plate]').getByText('Mounted', { exact: true }).count(), 0, 'pending verification must not claim mounted')
  pending = ''; current = ''
  idle = false
  await page.reload()
  await page.locator('[data-mesh-selector]').waitFor()
  await selector.selectOption('bb00000000000060')
  assert(await apply.isDisabled(), 'activation stays disabled during printing')
  assert.deepEqual(errors, [])
  console.log('PASS: library mesh selection is inert; physical-plate confirmation, exact plate/measure identity, verified/refused outcomes, explicit restart confirmation, busy guard and desktop/mobile layouts')
} finally { await browser?.close(); await server.close() }
