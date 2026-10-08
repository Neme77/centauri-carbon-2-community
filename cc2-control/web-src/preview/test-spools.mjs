import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Spools against the preview backend: switching tracking on and off, adding and weighing a spool,
// the "which spool is this?" question that every page opens for new filament, and the print dialog's
// check of the spool each tool would draw from.
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
    if (r.method() !== 'POST' || !r.url().includes('/api/spools/')) return
    assert.equal(r.headers()['x-cc2-request'], '1', 'spool changes carry the mutation marker')
    posts.push([new URL(r.url()).pathname, r.postData() || ''])
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  const toast = text => page.getByText(text, { exact: true }).waitFor()
  const card = name => page.locator('[data-spool]').filter({ has: page.getByText(name, { exact: true }) })
  const slot = n => page.locator(`[data-slot="${n}"]`)
  const insert = n => page.evaluate(n => fetch('/__preview/spool-insert', { method: 'POST', body: `slot=${n}` }), n)

  await page.goto(`${origin}/#spools`)
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('heading', { name: 'Spools', exact: true }).waitFor()
  // Off by default: the inventory is there, the trays and questions are not.
  await page.getByText('Tracking is off', { exact: false }).waitFor()
  assert.equal(await page.getByRole('heading', { name: 'In the Printer' }).count(), 0)
  await page.getByRole('button', { name: 'In use (4)' }).waitFor()
  await page.getByRole('button', { name: 'Archived (1)' }).waitFor()
  assert.equal(await page.locator('[data-spool]').count(), 4)

  await page.getByRole('button', { name: 'Turn on spool tracking' }).click()
  await toast('Spool tracking is on.')
  assert.deepEqual(posts.at(-1), ['/api/spools/enable', 'on'])
  await page.getByRole('heading', { name: 'In the Printer' }).waitFor()
  await slot(0).getByText('Red PLA', { exact: true }).waitFor()
  await slot(2).getByText('No spool: this slot is not counted.', { exact: true }).waitFor()
  await slot(4).getByText('External holder', { exact: true }).waitFor()
  // A spool below its warning level says so.
  await card('Green PLA').getByText('Running low', { exact: true }).waitFor()

  // A new spool: the material fills in the density, a net weight chip fills the remaining weight.
  await page.getByRole('button', { name: 'Add spool' }).click()
  let dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'New spool' }).waitFor()
  await dialog.getByLabel('Name', { exact: true }).fill('Silk Gold')
  await dialog.getByLabel('Material', { exact: true }).fill('PETG')
  assert.equal(await dialog.getByLabel('Density, g/cm³').inputValue(), '1.27')
  await dialog.getByLabel('Material', { exact: true }).fill('PLA Silk')
  assert.equal(await dialog.getByLabel('Density, g/cm³').inputValue(), '1.24')
  await dialog.getByLabel('Filament colour').last().fill('#C7A44A')
  await dialog.getByRole('button', { name: '750 g' }).click()
  assert.equal(await dialog.getByLabel('Remaining, g').inputValue(), '750')
  await dialog.getByRole('button', { name: 'Save', exact: true }).click()
  await toast('Spool saved.')
  assert.deepEqual(posts.at(-1), [
    '/api/spools/save',
    'name=Silk Gold\nbrand=\nmaterial=PLA Silk\ncolor=#C7A44A\nnet=750\nremaining=750\ntare=0\nlow=100\ndiameter=1.75\ndensity=1.24\nprice=0\nnote=',
  ])
  await card('Silk Gold').getByText('In storage', { exact: true }).waitFor()

  // Weighing subtracts the empty spool weight.
  await card('Red PLA').getByRole('button', { name: 'Weigh' }).click()
  dialog = page.getByRole('dialog')
  await dialog.getByLabel('Scale reading, g').fill('700')
  await dialog.getByText('Remaining filament: 550 g', { exact: true }).waitFor()
  await dialog.getByRole('button', { name: 'Save', exact: true }).click()
  await toast('Remaining set to 550 g.')
  assert.deepEqual(posts.at(-1), ['/api/spools/adjust', 'id=aa00000000000001\ngross=700'])
  await card('Red PLA').getByText('550 g', { exact: true }).waitFor()

  // New filament in tray 3: any page asks which spool it is, here the Dashboard. A spool whose filament
  // differs from the tray's is also written to the tray, so the printer and the slicer see it too.
  const controls = []
  await page.route('**/api/control', r => {
    controls.push(r.request().postData())
    return r.fulfill({ json: { accepted: true } })
  })
  await page.goto(`${origin}/#dashboard`)
  await insert(2)
  dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'New filament in Slot 3' }).waitFor()
  await dialog.getByText('The printer reports', { exact: true }).waitFor()
  await dialog.getByRole('radio', { name: /Black PETG/ }).check()
  assert.ok(await dialog.getByLabel("Also set Slot 3 on the printer to this spool's material and colour").isChecked())
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await toast('Command accepted')
  assert.deepEqual(posts.at(-1), ['/api/spools/assign', 'slot=2\nspool=aa00000000000004'])
  assert.deepEqual(controls, ['canvas:material:2:PETG:16191D:220:260'])
  await dialog.waitFor({ state: 'detached' })

  // "Not now" keeps the question open without asking again, and the menu marks it.
  await insert(1)
  await dialog.getByRole('heading', { name: 'New filament in Slot 2' }).waitFor()
  await dialog.getByRole('radio', { name: /Blue PLA/ }).waitFor() // the spool that was there is offered
  await dialog.getByRole('button', { name: 'Not now', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })
  await page.waitForTimeout(3500)
  assert.equal(await page.getByRole('dialog').count(), 0, 'a put-off question stays quiet')
  await page.locator('a[href="#spools"] i.bg-amber').waitFor()
  await page.goto(`${origin}/#spools`)
  await slot(1).getByText('Which spool?', { exact: true }).waitFor()
  // Answering from the page with a new spool creates it straight into the tray.
  await slot(1).getByRole('button', { name: 'Choose spool' }).click()
  dialog = page.getByRole('dialog')
  await dialog.getByRole('radio', { name: /New spool/ }).check()
  await dialog.getByLabel('Name', { exact: true }).fill('Fresh blue')
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await toast('Slot 2: spool “Fresh blue”.')
  const [route, created] = posts.at(-1)
  assert.equal(route, '/api/spools/save')
  assert.match(created, /^name=Fresh blue\n.*\ncolor=#42A5F5\n.*\nslot=1$/s)
  await slot(1).getByText('Fresh blue', { exact: true }).waitFor()

  // The print dialog shows each tool's spool and warns when it holds less than the file needs.
  await page.route('**/api/gcode-files/inspect', r =>
    r.fulfill({ json: { tools: [0], filaments: [{ tool: 0, color: '#EF5350', material: 'PLA', mm: 300000 }] } })
  )
  await page.route('**/api/canvas', r =>
    r.fulfill({
      json: {
        telemetry: {
          result: {
            canvas_info: {
              canvas_list: [
                {
                  connected: 1,
                  tray_list: [0, 1, 2, 3].map(tray_id => ({ tray_id, filament_type: 'PLA', filament_color: '#EF5350', status: 1 })),
                },
              ],
            },
          },
        },
      },
    })
  )
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.goto(`${origin}/#files`)
  await page.getByRole('button', { name: 'Print', exact: true }).first().click()
  dialog = page.getByRole('dialog')
  await dialog.locator('select').first().selectOption('0')
  await dialog.getByText('Red PLA: 550 g left, the file needs about 895 g', { exact: true }).waitFor()
  await dialog.getByText('A spool holds less filament than the file needs.', { exact: false }).waitFor()
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()

  // Phone width: the trays, the inventory and the question fit without sideways scrolling.
  const shots = process.env.CC2_SCREENSHOTS
  await page.goto(`${origin}/#spools`)
  await slot(0).waitFor()
  if (shots) await page.screenshot({ path: path.join(shots, 'spools-desktop.png'), fullPage: true })
  await page.setViewportSize({ width: 390, height: 844 })
  await page.waitForTimeout(300)
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, 'phone overflow')
  if (shots) await page.screenshot({ path: path.join(shots, 'spools-phone.png'), fullPage: true })
  await slot(2).getByRole('button', { name: 'Choose spool' }).click()
  dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'Spool in Slot 3' }).waitFor()
  assert.ok((await dialog.boundingBox()).width <= 390, 'the question fits a phone')
  if (shots) await page.screenshot({ path: path.join(shots, 'spools-chooser-phone.png') })
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.setViewportSize({ width: 1440, height: 1000 })

  // Off again: no more questions.
  await page.getByRole('button', { name: 'Turn off spool tracking' }).click()
  await toast('Spool tracking is off.')
  assert.deepEqual(posts.at(-1), ['/api/spools/enable', 'off'])
  assert.deepEqual(errors, [])
  console.log('PASS: spool tracking switch, new spool, weigh-in, slot question on any page, print check')
} finally {
  await browser?.close()
  await server.close()
}
