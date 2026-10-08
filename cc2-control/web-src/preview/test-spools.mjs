import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Spools against the preview backend: switching tracking on and off, the inventory grouped by
// manufacturer, kind and colour, adding, copying and weighing a spool, the "which spool is this?"
// question that every page opens for new filament, and the print dialog's check of the spool each
// tool would draw from.
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
  const texts = selector => page.locator(selector).evaluateAll(list => list.map(e => e.textContent))
  // A colour row: identical spools of one kind; its spools show once it is opened.
  const stack = label => page.locator('[data-stack]').filter({ has: page.getByText(label, { exact: true }) })
  const expand = async label => {
    const head = stack(label).locator('button').first()
    if ((await head.getAttribute('aria-expanded')) !== 'true') await head.click()
  }
  const row = id => page.locator(`[data-spool="${id}"]`)
  const slot = n => page.locator(`[data-slot="${n}"]`)
  const insert = n => page.evaluate(n => fetch('/__preview/spool-insert', { method: 'POST', body: `slot=${n}` }), n)

  await page.goto(`${origin}/#spools`)
  await page.selectOption('#preview-scene', 'idle')
  await page.getByRole('heading', { name: 'Spools', exact: true }).waitFor()
  // Off by default: the inventory is there, the trays and questions are not.
  await page.getByText('Tracking is off', { exact: false }).waitFor()
  assert.equal(await page.getByRole('heading', { name: 'In the Printer' }).count(), 0)
  await page.getByRole('button', { name: 'In use (7)' }).waitFor()
  await page.getByRole('button', { name: 'Archived (1)' }).waitFor()
  // Manufacturer > kind > colour: the two sealed black spools share a row, and Sunlu with two spools
  // goes to "Other". Colours follow the colour wheel, neutrals first; a spool without a colour in its
  // name gets a colour word.
  await page.getByText('Spools: 7 · 4.2 kg · colours running low: 1', { exact: true }).waitFor()
  assert.deepEqual(
    await page.locator('[data-brand]').evaluateAll(list => list.map(e => e.getAttribute('data-brand'))),
    ['elegoo', 'other']
  )
  assert.deepEqual(await texts('[data-brand] > button > b'), ['ELEGOO', 'Other'])
  assert.deepEqual(await texts('[data-kind] > div > b'), ['PETG', 'PLA', 'Sunlu PETG'])
  assert.deepEqual(await texts('[data-stack] > button > b'), ['Black', 'Red', 'Green', 'Blue', 'Orange', 'Purple'])
  await stack('Black').getByText('×2', { exact: true }).waitFor()
  assert.equal(await page.locator('[data-spool]').count(), 0, 'colour rows start closed')

  await page.getByRole('button', { name: 'Turn on spool tracking' }).click()
  await toast('Spool tracking is on.')
  assert.deepEqual(posts.at(-1), ['/api/spools/enable', 'on'])
  await page.getByRole('heading', { name: 'In the Printer' }).waitFor()
  await slot(0).getByText('Red PLA', { exact: true }).waitFor()
  await slot(2).getByText('No spool: this slot is not counted.', { exact: true }).waitFor()
  await slot(4).getByText('Without Canvas', { exact: true }).waitFor()
  await slot(4).getByText('such as TPU', { exact: false }).waitFor()
  // A colour whose spools hold less than their warning level says so, and the row shows where its spools are.
  await stack('Green').getByText('Running low', { exact: true }).waitFor()
  await stack('Green').getByText('Slot 4', { exact: true }).waitFor()

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
  // Without a brand it joins "Other"; its colour is the name without the material's words.
  await page
    .locator('[data-brand="other"] [data-kind]')
    .filter({ hasText: 'PLA Silk' })
    .getByText('Gold', { exact: true })
    .waitFor()
  await expand('Gold')
  await stack('Gold').getByText('Sealed', { exact: true }).waitFor()

  // Weighing subtracts the empty spool weight.
  await expand('Red')
  await row('aa00000000000001').getByRole('button', { name: 'Weigh' }).click()
  dialog = page.getByRole('dialog')
  await dialog.getByLabel('Scale reading, g').fill('700')
  await dialog.getByText('Remaining filament: 550 g', { exact: true }).waitFor()
  await dialog.getByRole('button', { name: 'Save', exact: true }).click()
  await toast('Remaining set to 550 g.')
  assert.deepEqual(posts.at(-1), ['/api/spools/adjust', 'id=aa00000000000001\ngross=700'])
  await row('aa00000000000001').getByText('550 g', { exact: true }).waitFor()
  await row('aa00000000000001').getByText('of 1000 g', { exact: false }).waitFor()

  // "Add another like this" copies the spool as a new, full one, and the colour row counts it.
  await expand('Black')
  await stack('Black').getByRole('button', { name: 'Add another like this' }).click()
  dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'New spool' }).waitFor()
  assert.equal(await dialog.getByLabel('Remaining, g').inputValue(), '1000')
  await dialog.getByRole('button', { name: 'Save', exact: true }).click()
  await stack('Black').getByText('×3', { exact: true }).waitFor()
  assert.deepEqual(posts.at(-1), [
    '/api/spools/save',
    'name=Black PETG\nbrand=ELEGOO\nmaterial=PETG\ncolor=#16191D\nnet=1000\nremaining=1000\ntare=150\nlow=100\ndiameter=1.75\ndensity=1.27\nprice=0\nnote=',
  ])

  // A search opens the colours it finds; a folded manufacturer stays folded after a reload.
  const search = page.getByRole('searchbox', { name: 'Search spools' })
  await search.fill('orange')
  await row('aa00000000000007').waitFor()
  assert.equal(await page.locator('[data-stack]').count(), 1)
  await search.fill('')
  await page.locator('[data-brand="elegoo"] > button').click()
  assert.equal(await page.locator('[data-brand="elegoo"] [data-stack]').count(), 0)
  await page.reload()
  await stack('Orange').waitFor()
  assert.equal(await page.locator('[data-brand="elegoo"] [data-stack]').count(), 0)
  await page.locator('[data-brand="elegoo"] > button').click()
  await stack('Red').waitFor()

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
  // The tray reports purple PLA and no spool has it: nothing unrelated is suggested, a new spool is preselected.
  await dialog.getByText('No spool in the inventory has this filament.', { exact: false }).waitFor()
  assert.equal(await dialog.locator('input[name="cc2-spool-choice"]').count(), 1)
  assert.ok(await dialog.getByRole('radio', { name: /New spool/ }).isChecked())
  // Every spool is one step away, grouped as in the inventory; the three sealed black spools are one choice.
  await dialog.getByRole('button', { name: /^Show all \d+ spools$/ }).click()
  await dialog.locator('[data-stack]').filter({ hasText: 'Black' }).getByRole('button').click()
  await dialog.getByRole('radio', { name: 'Black PETG · 1000 g · sealed ×3' }).check()
  await dialog.getByText('Chosen: Black PETG · 1000 g', { exact: true }).waitFor()
  assert.ok(await dialog.getByLabel("Also set Slot 3 on the printer to this spool's material and colour").isChecked())
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await toast('Command accepted')
  assert.deepEqual(posts.at(-1), ['/api/spools/assign', 'slot=2\nspool=aa00000000000004'])
  assert.deepEqual(controls, ['canvas:material:2:PETG:16191D:220:260'])
  await dialog.waitFor({ state: 'detached' })

  // "Not now" keeps the question open without asking again, and the menu marks it.
  await insert(1)
  await dialog.getByRole('heading', { name: 'New filament in Slot 2' }).waitFor()
  // The spool that was there fits what the tray reports (ELEGOO PLA Matte in its blue) and is preselected.
  assert.ok(await dialog.getByRole('radio', { name: /Blue PLA/ }).isChecked())
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
  // It starts as the tray's filament: the product line as its material, a colour word in its name.
  assert.equal(await dialog.getByLabel('Name', { exact: true }).inputValue(), 'ELEGOO PLA Matte Blue')
  assert.equal(await dialog.getByLabel('Material', { exact: true }).inputValue(), 'PLA Matte')
  await dialog.getByLabel('Name', { exact: true }).fill('Fresh blue')
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await toast('Slot 2: spool “Fresh blue”.')
  const [route, created] = posts.at(-1)
  assert.equal(route, '/api/spools/save')
  assert.match(created, /^name=Fresh blue\n.*\ncolor=#42A5F5\n.*\nslot=1$/s)
  await slot(1).getByText('Fresh blue', { exact: true }).waitFor()
  // Slot 4 holds the only green PLA: choosing again offers just that spool, a new one or none.
  await slot(3).getByRole('button', { name: 'Choose spool' }).click()
  dialog = page.getByRole('dialog')
  assert.ok(await dialog.getByRole('radio', { name: /Green PLA/ }).isChecked())
  assert.equal(await dialog.locator('input[name="cc2-spool-choice"]').count(), 3)
  assert.equal(await dialog.getByText('No spool in the inventory has this filament.', { exact: false }).count(), 0)
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  await dialog.waitFor({ state: 'detached' })

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

  // Phone width: the trays, the inventory with an open colour and the question fit without sideways scrolling.
  const shots = process.env.CC2_SCREENSHOTS
  await page.goto(`${origin}/#spools`)
  await slot(0).waitFor()
  await expand('Black')
  if (shots) await page.screenshot({ path: path.join(shots, 'spools-desktop.png'), fullPage: true })
  await page.setViewportSize({ width: 390, height: 844 })
  await page.waitForTimeout(300)
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, 'phone overflow')
  if (shots) await page.screenshot({ path: path.join(shots, 'spools-phone.png'), fullPage: true })
  await slot(2).getByRole('button', { name: 'Choose spool' }).click()
  dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'Spool in Slot 3' }).waitFor()
  assert.ok((await dialog.boundingBox()).width <= 390, 'the question fits a phone')
  await dialog.getByRole('button', { name: /^Show all \d+ spools$/ }).click()
  await dialog.locator('[data-stack]').filter({ hasText: 'Black' }).getByRole('button').click()
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, 'tree overflow')
  if (shots) await page.screenshot({ path: path.join(shots, 'spools-chooser-phone.png') })
  await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.setViewportSize({ width: 1440, height: 1000 })

  // Off again: no more questions.
  await page.getByRole('button', { name: 'Turn off spool tracking' }).click()
  await toast('Spool tracking is off.')
  assert.deepEqual(posts.at(-1), ['/api/spools/enable', 'off'])
  assert.deepEqual(errors, [])
  console.log(
    'PASS: spool tracking switch, inventory by manufacturer/kind/colour, new and copied spool, weigh-in, slot question on any page, print check'
  )
} finally {
  await browser?.close()
  await server.close()
}
