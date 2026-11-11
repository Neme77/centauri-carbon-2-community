import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Print queue against the preview backend: the page with its next-job card, pre-flight check, copies,
// order and policy; the Files chip; the print dialog's "Add to queue" choice; the Dashboard's quick start.
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
    if (r.method() !== 'POST' || !r.url().includes('/api/queue/')) return
    assert.equal(r.headers()['x-cc2-request'], '1', 'queue changes carry the mutation marker')
    posts.push([new URL(r.url()).pathname, r.postData() || ''])
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  // The file's filaments and the saved meshes come from the printer; the preview answers for it here.
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0], filaments: [{ tool: 0, color: '#1a1a1a', material: 'PLA' }], bed_temperature: 60, nozzle_diameter: 0.4 } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {}, default1: {} } } } } } }))
  await page.goto(`${origin}/#queue`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')])
  await page.getByRole('heading', { name: 'Print queue' }).waitFor()
  const lastPost = () => posts.at(-1)

  // The head of the queue, its copy counter and the pre-flight check.
  const hero = page.locator('[data-queue-hero="next"]')
  await hero.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).waitFor()
  await hero.getByText('Copy 3 of 5', { exact: true }).waitFor()
  await hero.locator('[data-preflight] li').nth(2).waitFor()
  assert.ok((await hero.locator('[data-preflight] li').count()) >= 4, 'file, feed, mesh and printer are checked')
  await hero.locator('[data-preflight] li[data-tone="ok"]').first().waitFor()
  const rows = page.locator('[data-queue-item]')
  await rows.nth(3).waitFor()
  assert.equal(await rows.count(), 4, 'finished jobs are not among the open ones')
  await rows.nth(3).getByText('File missing', { exact: true }).waitFor()
  assert.equal(await page.locator('[data-queue-count]').textContent(), '4', 'the menu counts the open jobs')
  await page.locator('[data-queue-totals]').getByText('4', { exact: true }).first().waitFor()

  // Start: one POST, the toast names the copy; the copy then runs until the printer reports it finished,
  // and nothing else can start meanwhile.
  await hero.locator('[data-queue-start]').click()
  await page.getByText('Copy 3 of 5 requested: CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/start', '1'])
  const active = page.locator('[data-queue-hero="active"]')
  await active.waitFor()
  assert.equal(await active.locator('[data-queue-start]').isDisabled(), true, 'no start while a copy runs')
  await rows.nth(0).getByText('Starting…', { exact: true }).waitFor()
  assert.equal(await rows.nth(0).locator('[data-queue-remove]').isDisabled(), true, 'the running job cannot be removed')
  const complete = () => page.request.post(`${origin}/__preview/queue-complete`, { headers: { 'X-CC2-Request': '1' } })
  assert.equal((await complete()).status(), 200)
  await hero.getByText('Copy 4 of 5', { exact: true }).waitFor()
  await page.waitForFunction(() => !document.querySelector('[data-queue-hero="next"] [data-queue-start]')?.hasAttribute('disabled'))
  assert.equal(await hero.locator('[data-queue-start]').isEnabled(), true, 'the next copy can start once the copy is counted')

  // Copies: the stepper of the second job.
  const second = rows.nth(1)
  await second.getByRole('button', { name: 'More copies', exact: true }).click()
  await second.locator('[data-queue-copies]').getByText('3', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/copies', '2\n3'])

  // Order: the second job moves below the third.
  await second.getByRole('button', { name: 'Move down', exact: true }).click()
  await rows.nth(1).getByText('CC2_Preview_Phone_stand.gcode', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/reorder', '2\n2'])
  await rows.nth(2).getByText('CC2_Preview_Gear_housing_PETG.gcode', { exact: true }).waitFor()

  // Remove asks first.
  await rows.nth(3).locator('[data-queue-remove]').click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('Remove CC2_Preview_Vase_missing.gcode from the queue?', { exact: true }).waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('[data-queue-item]').length === 3)
  assert.deepEqual(lastPost(), ['/api/queue/remove', '4'])

  // Policy: the automatic start is a saved choice, the delay a number.
  const policy = page.locator('[data-queue-policy]')
  await policy.getByRole('radio', { name: 'Start the next copy automatically', exact: false }).check()
  await page.waitForFunction(() => document.querySelector('[data-queue-policy] input[type=radio]:nth-of-type(1)') !== null)
  assert.deepEqual(lastPost(), ['/api/queue/policy', 'auto\n60\n0'])
  await policy.getByRole('switch').click()
  await page.waitForFunction(() => document.querySelector('[data-queue-policy] [role=switch]')?.getAttribute('aria-checked') === 'true')
  assert.deepEqual(lastPost(), ['/api/queue/policy', 'auto\n60\n1'])

  // Finished jobs are folded away and can be cleared.
  await page.getByRole('button', { name: 'Finished (1)', exact: true }).click()
  await page.locator('[data-queue-done="5"]').waitFor()
  await page.getByRole('button', { name: 'Clear finished', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[data-queue-done]'))
  assert.deepEqual(lastPost(), ['/api/queue/clear-done', ''])

  // Files: the queued file carries its chip; Print opens the dialog with the queue choice.
  await page.goto(`${origin}/#files`)
  const buddha = page.locator('.cc2-file-row').filter({ hasText: 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode' })
  await buddha.locator('[data-queued-chip]').getByText('In queue · 2', { exact: true }).waitFor()
  await buddha.getByRole('button', { name: 'Print', exact: true }).click()
  const print = page.getByRole('dialog')
  await print.locator('[data-print-launch]').waitFor()
  assert.equal(await print.locator('[data-print-submit]').textContent(), 'Start print')
  await print.locator('[data-launch="queue"]').click()
  await print.locator('[data-print-copies]').waitFor()
  await print.getByRole('button', { name: 'More copies', exact: true }).click()
  await print.getByRole('button', { name: 'More copies', exact: true }).click()
  assert.equal(await print.locator('[data-print-copies]').inputValue(), '3')
  assert.equal(await print.locator('[data-print-submit]').textContent(), 'Add to queue ×3')
  await print.getByRole('radio', { name: 'Next', exact: true }).check()
  await print.locator('[data-print-submit]').click()
  await page.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode added to the queue.', { exact: true }).waitFor()
  const add = posts.find(p => p[0] === '/api/queue/add')
  assert.ok(add, 'the dialog adds through /api/queue/add')
  const lines = add[1].split('\n')
  assert.equal(lines[0], 'internal')
  assert.equal(lines[1], 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode')
  assert.equal(lines[8], '3', 'three copies')
  assert.equal(lines[9], 'next', 'queued as the next job')
  assert.equal(lines[10], '1547', 'the slicer estimate travels with the job')
  await page.waitForFunction(() => !document.querySelector('[role=dialog]'))
  await buddha.locator('[data-queued-chip]').getByText('In queue · 5', { exact: true }).waitFor()

  // Dashboard: the quick start for the next copy while the printer is idle.
  await page.goto(`${origin}/#dashboard`)
  const quick = page.locator('[data-dashboard-queue]')
  await quick.getByText('Next in queue', { exact: true }).waitFor()
  await quick.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).first().waitFor()
  await quick.locator('[data-dashboard-queue-start]').click()
  await page.getByText('Copy 1 of 3 requested: CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).waitFor()
  assert.equal(lastPost()[0], '/api/queue/start')
  await quick.waitFor({ state: 'hidden' }) // a running copy hides the quick start
  assert.equal((await complete()).status(), 200)
  await quick.getByText('Copy 2 of 3', { exact: false }).waitFor() // the counter shares its line with the estimate
  // Under the automatic policy the counted copy arms the delay; the countdown shows where the operator looks,
  // and its button cancels the start.
  const countdown = quick.locator('[data-dashboard-queue-auto]')
  await countdown.waitFor()
  assert.match(await countdown.textContent(), /^Automatic start in \d+ s/) // the text node shares the box with the button
  await countdown.getByRole('button', { name: 'Cancel automatic start', exact: true }).click()
  await countdown.waitFor({ state: 'hidden' })
  assert.deepEqual(lastPost(), ['/api/queue/hold', ''])

  // A job is bound to a filament, not to a slot. Added with slot 2, it takes the filament that slot is labelled
  // with; a start goes to whichever slot holds that filament, and a job whose filament is in no slot cannot
  // start, wherever a start is offered.
  const send = (to, data) => page.request.post(`${origin}${to}`, { headers: { 'X-CC2-Request': '1' }, data })
  const noSlot = 'Slot 2 is empty and no other slot holds the same filament. The job cannot start'
  const added = await send('/api/queue/add', ['internal', 'CC2_Preview_Slot_check.gcode', '0:1', 'A', 'saved', '0', '', '', '1', 'next'].join('\n'))
  assert.equal(added.status(), 201)
  const slotJob = (await added.json()).id
  await quick.getByText('CC2_Preview_Slot_check.gcode', { exact: true }).first().waitFor()
  const quickFeed = quick.locator('[data-dashboard-queue-filaments] [data-queue-feed]')
  await quick.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await quickFeed.textContent(), 'PLA · Slot 2')
  assert.equal((await send('/__preview/canvas-tray', '1\n0')).status(), 200) // the spool of slot 2 runs out
  await quick.locator('[data-queue-filament-notes]').getByText(noSlot, { exact: true }).waitFor()
  assert.equal(await quickFeed.getAttribute('data-queue-feed'), 'blocked')
  assert.equal(await quickFeed.textContent(), 'PLA · Slot 2 empty')
  assert.equal(await quick.locator('[data-dashboard-queue-start]').isDisabled(), true, 'no quick start without the filament')
  assert.equal((await send('/__preview/canvas-tray', '3\n1\n#42a5f5')).status(), 200) // slot 4 now holds the same filament
  await quick.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await quickFeed.textContent(), 'PLA · Slot 4')
  assert.equal(await quick.locator('[data-queue-filament-notes]').count(), 0, 'a filament found in another slot is nothing to warn about')
  assert.equal(await quick.locator('[data-dashboard-queue-start]').isEnabled(), true, 'the quick start takes the slot that holds the filament')
  assert.equal((await send('/__preview/canvas-tray', '3\n1\n#66bb6a')).status(), 200)
  await page.unroute('**/api/canvas') // the page checks the slot that will feed the print against the file
  await page.goto(`${origin}/#queue`)
  await hero.getByText('CC2_Preview_Slot_check.gcode', { exact: true }).waitFor()
  await hero.locator('[data-preflight] li[data-tone="bad"]').getByText(noSlot, { exact: true }).waitFor()
  const heroFeed = hero.locator('[data-queue-feed]')
  assert.equal(await heroFeed.getAttribute('data-queue-feed'), 'blocked')
  assert.equal(await heroFeed.textContent(), 'PLA · Slot 2 empty')
  assert.equal(await hero.locator('[data-queue-start]').isDisabled(), true, 'no start while the filament is in no slot')
  assert.equal((await send('/__preview/canvas-tray', '3\n1\n#42a5f5')).status(), 200)
  await hero.locator('[data-preflight] li[data-tone="ok"]').getByText('Slot 4: PLA — as in the file', { exact: true }).waitFor()
  await hero.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await heroFeed.textContent(), 'PLA · Slot 4')
  assert.equal(await hero.locator('[data-preflight] li[data-tone="bad"]').count(), 0)
  // The list shows every job's filament and where it is now; the external feed has none.
  const feedOf = name => page.locator('[data-queue-item]').filter({ hasText: name }).locator('[data-queue-feed]')
  assert.equal(await feedOf('CC2_Preview_Slot_check.gcode').textContent(), 'PLA · Slot 4')
  assert.equal(await feedOf('CC2_Preview_Gear_housing_PETG.gcode').getAttribute('data-queue-feed'), 'ready')
  assert.equal(await feedOf('CC2_Preview_Gear_housing_PETG.gcode').textContent(), 'PLA · Slot 1')
  assert.equal(await feedOf('CC2_Preview_Phone_stand.gcode').getAttribute('data-queue-feed'), 'external')
  assert.equal(await feedOf('CC2_Preview_Phone_stand.gcode').textContent(), 'External feed')
  await page.waitForFunction(() => !document.querySelector('[data-queue-hero="next"] [data-queue-start]')?.hasAttribute('disabled'))
  await hero.locator('[data-queue-start]').click()
  await page.getByText('Copy 1 of 1 requested: CC2_Preview_Slot_check.gcode', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/start', String(slotJob)])
  // A running copy shows its filament alone: where the next start would look is not this print's matter.
  const running = page.locator('[data-queue-hero="active"] [data-queue-feed]')
  await running.waitFor()
  assert.equal(await running.getAttribute('data-queue-feed'), 'running')
  assert.equal(await running.textContent(), 'PLA')

  // Another filament goes into the slot of a job. The job does not print with it: it waits for its own, in any
  // slot, and says so wherever a start is offered. The operator can give the job the filament a slot holds now.
  assert.equal((await complete()).status(), 200) // the copy of the slot check is counted
  const absent = "This job's filament is in no Canvas slot. Load it into any slot and set that slot's filament, or give the job another filament. The job cannot start"
  const fileWants = 'Slot 1: PETG now, the file wants PLA'
  const fed = await send('/api/queue/add', ['internal', 'CC2_Preview_Filament_check.gcode', '0:0', 'A', 'saved', '0', '', '', '2', 'next'].join('\n'))
  assert.equal(fed.status(), 201)
  const fedJob = (await fed.json()).id
  await hero.getByText('CC2_Preview_Filament_check.gcode', { exact: true }).waitFor()
  await hero.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await heroFeed.textContent(), 'PLA · Slot 1')
  const chipDot = await heroFeed.locator('span').boundingBox()
  assert.ok(chipDot && Math.round(chipDot.width) === 12 && Math.round(chipDot.height) === 12, `the chip's colour dot is 12 px: ${JSON.stringify(chipDot)}`)
  assert.equal((await send('/__preview/canvas-tray', '0\n1\n#101010\nPETG')).status(), 200) // another spool goes into slot 1
  await hero.locator('[data-preflight] li[data-tone="bad"]').getByText(absent, { exact: true }).waitFor()
  await hero.locator('[data-queue-feed="blocked"]').waitFor()
  assert.equal(await heroFeed.textContent(), 'PLA · not in the printer')
  const remembered = hero.locator('[data-preflight] [data-queue-filament]')
  assert.equal((await remembered.textContent()).replace(/\s+/g, ' ').trim(), "This job's filament: PLA")
  const dot = await remembered.locator('span').boundingBox()
  assert.ok(dot && Math.round(dot.width) === 12 && Math.round(dot.height) === 12, `the colour dot is 12 px: ${JSON.stringify(dot)}`)
  assert.equal(await hero.locator('[data-queue-start]').isDisabled(), true, 'the job does not print with another filament')
  assert.equal(await hero.locator('[data-preflight]').getByText(fileWants, { exact: true }).count(), 0, 'slot 1 is not where this job prints from')
  assert.equal(await feedOf('CC2_Preview_Gear_housing_PETG.gcode').getAttribute('data-queue-feed'), 'blocked') // it was added with that filament too
  assert.equal(await feedOf('CC2_Preview_Phone_stand.gcode').textContent(), 'External feed')
  assert.equal((await send('/__preview/canvas-tray', '2\n1\n#ef5350')).status(), 200) // slot 3 now holds the job's filament
  await hero.locator('[data-preflight] li[data-tone="ok"]').getByText('Slot 3: PLA — as in the file', { exact: true }).waitFor()
  await hero.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await heroFeed.textContent(), 'PLA · Slot 3')
  await page.waitForFunction(() => !document.querySelector('[data-queue-hero="next"] [data-queue-start]')?.hasAttribute('disabled'))
  await page.goto(`${origin}/#dashboard`)
  await quick.getByText('CC2_Preview_Filament_check.gcode', { exact: true }).first().waitFor()
  await quick.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await quickFeed.textContent(), 'PLA · Slot 3')
  assert.equal((await send('/__preview/canvas-tray', '2\n1\n#fdd835')).status(), 200) // and no longer does
  await quick.locator('[data-queue-filament-notes]').getByText(absent, { exact: true }).waitFor()
  assert.equal(await quick.locator('[data-queue-filament-notes] li').count(), 1)
  assert.equal(await quick.locator('[data-queue-filament-notes] [data-queue-filament]').count(), 1)
  assert.equal(await quickFeed.textContent(), 'PLA · not in the printer')
  assert.equal(await quick.locator('[data-dashboard-queue-start]').isDisabled(), true)
  // The operator's way out: the job takes the filament that slot 1 holds now. Spool tracking is off, so only
  // the slots are on offer.
  await page.goto(`${origin}/#queue`)
  await hero.getByText('CC2_Preview_Filament_check.gcode', { exact: true }).waitFor()
  await hero.locator('[data-queue-filament-edit]').click()
  const change = page.locator('[data-queue-filament-dialog]')
  await change.getByText('Filament of the job', { exact: true }).waitFor()
  assert.equal((await change.locator('[data-queue-filament]').textContent()).replace(/\s+/g, ' ').trim(), "This job's filament: PLA")
  assert.equal(await change.locator('[data-feed-stock]').count(), 0)
  assert.equal(await change.locator('[data-queue-filament-save]').isDisabled(), true, 'nothing chosen, nothing to save')
  await change.locator('input[name="queue-tool-0"][value="0"]').check()
  await change.locator('[data-queue-filament-save]').click()
  await page.getByText("The job's filament is changed.", { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/filament', `${fedJob}\n0:0`])
  await change.waitFor({ state: 'hidden' })
  await hero.locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await heroFeed.textContent(), 'PETG · Slot 1')
  // The file's material is still checked against the slot that will feed the print.
  await hero.locator('[data-preflight] li[data-tone="warn"]').getByText(fileWants, { exact: true }).waitFor()
  assert.equal(await hero.locator('[data-preflight] [data-queue-filament]').count(), 0)
  await page.waitForFunction(() => !document.querySelector('[data-queue-hero="next"] [data-queue-start]')?.hasAttribute('disabled'))
  await hero.locator('[data-queue-start]').click()
  await page.getByText('Copy 1 of 2 requested: CC2_Preview_Filament_check.gcode', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/start', String(fedJob)])
  assert.equal((await complete()).status(), 200)
  await hero.getByText('Copy 2 of 2', { exact: true }).waitFor()
  assert.equal(await heroFeed.textContent(), 'PETG · Slot 1')

  // A job for a filament that is not in the printer: with spool tracking on, the print dialog offers the
  // filaments of the inventory to a job that goes to the queue, and the job waits until a slot holds its own.
  assert.equal((await send('/api/spools/enable', 'on')).status(), 200)
  await page.goto(`${origin}/#files`)
  // The app learns that tracking is on from its printer poll: the second reply from now means the first is in.
  for (let i = 0; i < 2; i++) await page.waitForResponse(r => new URL(r.url()).pathname === '/api/printer')
  const stand = page.locator('.cc2-file-row').filter({ hasText: 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode' })
  await stand.getByRole('button', { name: 'Print', exact: true }).click()
  await print.locator('[data-print-launch]').waitFor()
  const stockRow = print.locator('[data-feed-stock="0"]')
  const stockRadio = stockRow.locator('input[type=radio]')
  await stockRow.waitFor()
  assert.equal(await stockRadio.isDisabled(), true, 'a print that starts now needs a slot')
  const closedBox = await stockRow.boundingBox()
  await print.locator('[data-launch="queue"]').click()
  await page.waitForFunction(() => !document.querySelector('[data-feed-stock="0"] input[type=radio]')?.hasAttribute('disabled'))
  assert.deepEqual(await stockRow.boundingBox(), closedBox, 'the choice does not move when the queue is chosen')
  // The inventory opens in three steps: the brand, then one of its materials, then one of that material's
  // colours, each with its square. Nothing is chosen until the colour is.
  await stockRadio.check()
  const level = name => stockRow.locator(`[data-stock-level="${name}"]`).getByRole('radio')
  const chosen = async () => (await stockRow.locator('[data-stock-choice]').textContent()).trim()
  assert.deepEqual(await level('brand').allTextContents(), ['ELEGOO', 'Sunlu', 'Polymaker'])
  assert.equal(await stockRow.locator('[data-stock-level="kind"]').count(), 0, 'the materials follow the brand')
  assert.equal(await chosen(), 'Not set')
  await print.locator('[data-print-submit]').click()
  await print.getByText('Choose the brand, the material and the colour of the filament.', { exact: true }).waitFor()
  await level('brand').filter({ hasText: 'ELEGOO' }).click()
  assert.deepEqual(await level('kind').allTextContents(), ['PETG', 'PLA'])
  assert.equal(await stockRow.locator('[data-stock-level="colour"]').count(), 0, 'the colours follow the material')
  await level('kind').filter({ hasText: 'PLA' }).click()
  assert.deepEqual(await level('colour').allTextContents(), ['Red', 'Green', 'Blue'])
  await level('kind').filter({ hasText: 'PETG' }).click()
  assert.deepEqual(await level('colour').allTextContents(), ['Black'])
  const black = level('colour').filter({ hasText: 'Black' })
  const square = await black.locator('span').boundingBox()
  assert.ok(square && Math.round(square.width) === 16 && Math.round(square.height) === 16, `the colour has its square: ${JSON.stringify(square)}`)
  assert.equal(await black.locator('span').evaluate(e => getComputedStyle(e).backgroundColor), 'rgb(22, 25, 29)')
  await black.click()
  assert.equal(await black.getAttribute('aria-checked'), 'true')
  assert.equal(await stockRadio.isChecked(), true)
  assert.equal(await chosen(), 'ELEGOO · PETG · Black')
  // The three steps taken are marked alike, and unlike the choices left aside (once the colours have settled).
  await page.waitForFunction(() => {
    const border = e => getComputedStyle(e).borderTopColor
    const row = document.querySelector('[data-feed-stock="0"]')
    const taken = [...row.querySelectorAll('[role=radio][aria-checked="true"]')].map(border)
    const aside = [...row.querySelectorAll('[role=radio][aria-checked="false"]')].map(border)
    return taken.length === 3 && new Set(taken).size === 1 && aside.length === 3 && !aside.includes(taken[0])
  })
  // A brand with one material takes that step at once; another brand starts the choice again.
  await level('brand').filter({ hasText: 'Polymaker' }).click()
  assert.deepEqual(await level('kind').allTextContents(), ['PLA'])
  assert.deepEqual(await level('colour').allTextContents(), ['Blue'])
  assert.equal(await chosen(), 'Not set')
  await level('brand').filter({ hasText: 'ELEGOO' }).click()
  await level('kind').filter({ hasText: 'PETG' }).click()
  await black.click()
  assert.equal(await chosen(), 'ELEGOO · PETG · Black')
  // Such a choice is no slot: a print that starts now is not sent with it, and nothing moves meanwhile.
  const openBox = await stockRow.boundingBox()
  await print.locator('[data-launch="now"]').click()
  await page.waitForFunction(() => document.querySelector('[data-feed-stock="0"] input[type=radio]')?.hasAttribute('disabled'))
  assert.deepEqual(await stockRow.boundingBox(), openBox, 'the open choice keeps its place when the print is to start now')
  assert.equal(await black.isDisabled(), true)
  await print.locator('[data-print-submit]').click()
  await print.getByText('Choose a Canvas slot for every filament.', { exact: true }).waitFor()
  assert.equal(await print.locator('[data-print-submit]').isEnabled(), true, 'nothing was submitted')
  await print.locator('[data-launch="queue"]').click()
  await print.locator('[data-print-submit]').click()
  await page.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode added to the queue.', { exact: true }).waitFor()
  const waitingAdd = lastPost()[1].split('\n')
  assert.equal(lastPost()[0], '/api/queue/add')
  assert.equal(waitingAdd[2], '', 'no slot is named for the tool')
  assert.equal(waitingAdd[12], '0\t\tPETG\t#16191D', 'the material and the colour travel with the job')
  await page.goto(`${origin}/#queue`)
  const waitingFeed = page.locator('[data-queue-item]').last().locator('[data-queue-feed]')
  await waitingFeed.waitFor()
  assert.equal(await waitingFeed.getAttribute('data-queue-feed'), 'blocked')
  assert.equal(await waitingFeed.textContent(), 'PETG · not in the printer')
  assert.equal((await send('/__preview/canvas-tray', '2\n1\n#16191D\nPETG')).status(), 200) // the black PETG is loaded into slot 3
  await page.locator('[data-queue-item]').last().locator('[data-queue-feed="ready"]').waitFor()
  assert.equal(await waitingFeed.textContent(), 'PETG · Slot 3')
  // A queued job can be given a filament of the inventory as well, by the same three steps: blue PLA is in
  // slot 4 (slot 2, labelled with it, is empty). The job is saved once the choice is whole.
  await hero.locator('[data-queue-filament-edit]').click()
  const other = change.locator('[data-feed-stock="0"]')
  await other.locator('input[type=radio]').check()
  assert.equal(await change.locator('[data-queue-filament-save]').isDisabled(), true, 'the choice is not whole yet')
  await other.locator('[data-stock-level="brand"]').getByRole('radio', { name: 'Polymaker', exact: true }).click()
  assert.equal(await change.locator('[data-queue-filament-save]').isDisabled(), true)
  await other.locator('[data-stock-level="colour"]').getByRole('radio', { name: 'Blue', exact: true }).click()
  assert.equal((await other.locator('[data-stock-choice]').textContent()).trim(), 'Polymaker · PLA · Blue')
  await change.locator('[data-queue-filament-save]').click()
  await change.waitFor({ state: 'hidden' })
  assert.deepEqual(lastPost(), ['/api/queue/filament', `${fedJob}\n\n0\t\tPLA\t#42A5F5`])
  await hero.locator('[data-queue-feed="ready"]').waitFor()
  await page.waitForFunction(() => document.querySelector('[data-queue-hero="next"] [data-queue-feed]')?.textContent === 'PLA · Slot 4')

  assert.deepEqual(errors, [])
  console.log('PASS: print queue page, Files chip, dialog choice, Dashboard quick start, automatic countdown, jobs bound to filaments (the slot that holds one, a filament in no slot, another filament for a job, a job that waits for a filament of the inventory chosen by brand, material and colour)')
} finally {
  await browser?.close()
  await server.close()
}
