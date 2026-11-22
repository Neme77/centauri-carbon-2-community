// The screw page: short info box + folded help, the quick measurement says what it leaves out and
// sits beside "Start session", the session actions are reachable right under the bed picture, and a running
// measurement says honestly what it contains (no countdown the service cannot know).
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'


const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
const shots = path.join(root, 'preview/artifacts')
const errors = []
let browser
const box = async locator => {
  const b = await locator.boundingBox()
  assert.ok(b, 'element has a box')
  return b
}
try {
  await server.listen()
  await mkdir(shots, { recursive: true })
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH
      ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] }
      : process.env.CC2_BROWSER_EXECUTABLE
        ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE }
        : {}),
  })
  // Tall enough that step 2 with its help fits without scrolling (a click on the help would scroll the page).
  const page = await browser.newPage({ viewport: { width: 1440, height: 1200 } })
  await page.addInitScript(() => { try { localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' })) } catch {} })
  page.on('pageerror', e => errors.push(e.message))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.locator('#cc2-navigation a[href="#bed"]').click()
  await page.getByRole('tab', { name: 'Bed alignment', exact: true }).click()
  const card = page.locator('#screwFocus')
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  const start = card.getByRole('button', { name: /Start session/ })
  const quick = card.getByRole('button', { name: /Measure only \(quick\)/ })
  const check = card.getByRole('button', { name: /Measure again/ })

  const mode = name => card.locator(`[data-level-start-mode-button="${name}"]`).click()
  // The hold time of step 1 goes in front of every start the user makes; 0 here (test-bed-mesh-tick covers it).
  await card.locator('[data-level-soak]').selectOption('0')
  const sec1 = card.locator('[data-level-step="1"]')
  const sec2 = card.locator('[data-level-step="2"]')

  // Point 1: the note about the reference is gone (the table says "Reference" and "Within tolerance"); the rest of the
  // explanation sits in a folded help under the table.
  assert.equal(await card.locator('[data-screw-info]').count(), 0, 'no separate note about the reference point')
  assert.equal(await card.getByText('Front left is the reference point.', { exact: true }).count(), 0)
  const help = card.locator('details[data-screw-help]')
  assert.equal(await help.evaluate(e => e.open), false, 'the help is folded')
  assert.equal(await card.getByText('Mesh capture stays separate.').isVisible(), false)
  await help.locator('summary').click()
  await card.getByText('Mesh capture stays separate.').waitFor()
  await card.getByText('A positive difference means this corner is higher').waitFor()
  await help.locator('summary').click()

  // Point 2: the start of every mode is one button with one short line; the temperature is not repeated in it.
  assert.equal(await quick.getByText(/Bed 60 °C/).count(), 0, 'the quick button does not repeat the temperature')
  // Before a session step 1 shows the bed only (live, then the target), never the nozzle.
  assert.equal(await card.locator('#screw-temp-values [data-temp-row]').count(), 1, 'before a session only the bed row')
  await card.locator('#screw-temp-values [data-temp-row="bed"] [data-value="live"]').waitFor()
  await card.locator('#screw-temp-values [data-temp-row="bed"] [data-value="target"]').getByText('Target 60 °C').waitFor()
  assert.equal(await card.locator('#screw-temp-values [data-temp-row="nozzle"]').count(), 0, 'no nozzle row before a session')
  await quick.getByText('No wait, heaters off afterwards').waitFor()
  assert.equal(await quick.getByText(/no own homing/i).count(), 0, 'the quick measurement does home (G180 S3 runs G28)')
  await mode('session')
  await start.getByText('30 s wait before each measurement, off after 10 min idle').waitFor()
  assert.equal(await start.getByText(/Bed 60 °C/).count(), 0, 'the start button does not repeat the temperature')
  await mode('quick')

  // Step 1 does not change its height with the way of measuring: the frame stays where it is (no jumping).
  const heights = []
  for (const m of ['quick', 'session', 'quick']) {
    await mode(m)
    heights.push(Math.round((await box(sec1)).height))
  }
  assert.equal(new Set(heights).size, 1, `step 1 has the same height in every mode (${heights.join(' / ')})`)

  // No accuracy figure for the quick measurement is claimed: none has been measured.
  assert.equal(await card.getByText(/Accuracy compared with a session|deviation about/).count(), 0, 'no accuracy claim')
  await quick.click()
  const quickDialog = page.getByRole('dialog')
  await quickDialog.getByText(/Home all axes, heat the bed to 60 °C/).waitFor()
  assert.equal(await quickDialog.getByText(/Accuracy compared with a session/).count(), 0, 'no placeholder in the quick dialog')
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(await quickDialog.getByText(/Bed too hot/).count(), 0, 'a bed at its normal temperature: no warning')
  // A bed clearly hotter than the target (more than 2.5 K): the start warns that cooling takes extra time and cannot be
  // interrupted. Up to 2.5 K above the target there is no note: the printer regulates from there.
  let bedNow = 62.5
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.heater_bed = { ...data.heater_bed, temperature: bedNow }
    await route.fulfill({ response, json: data })
  })
  const liveBed = text => page.locator('#screw-temp-values [data-temp-row="bed"] [data-value="live"]').getByText(text).waitFor({ timeout: 15000 })
  await liveBed(/^62\.5/)
  await quick.click()
  await quickDialog.getByText(/Home all axes, heat the bed to 60 °C/).waitFor()
  assert.equal(await quickDialog.getByText(/Bed too hot/).count(), 0, '2.5 K above the target: no note')
  // The note is live: it comes while the question is open, follows the bed and goes away again.
  bedNow = 100
  await quickDialog.getByText(/Bed too hot: it is at 100\.0 °C, the target is 60 °C\./).waitFor({ timeout: 15000 })
  await quickDialog.getByText(/cooling down takes extra time and cannot be interrupted meanwhile/).waitFor()
  await quickDialog.getByText(/cools first with the nozzle off/).waitFor()
  bedNow = 62.6
  await quickDialog.getByText(/Bed too hot: it is at 62\.6 °C, the target is 60 °C\./).waitFor({ timeout: 15000 })
  bedNow = 62.4
  await quickDialog.getByText(/Bed too hot/).waitFor({ state: 'detached', timeout: 15000 })
  await quickDialog.getByText(/Home all axes, heat the bed to 60 °C/).waitFor()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.unroute('**/api/printer')

  // Position 1: the intro sits under the card title (in the card head), not as a paragraph of its own.
  const intro = card.locator('.cc2-card-head small').getByText('Measures the four bed screws and evaluates them', { exact: false })
  await intro.waitFor()
  const introBox = await box(intro)
  const s1 = await box(sec1)
  assert.ok(introBox.y + introBox.height <= s1.y, 'the intro is above step 1')
  assert.equal(await card.getByText('Measures the four bed screws and evaluates them', { exact: false }).count(), 1, 'the intro is there once')
  assert.equal(await card.getByText('step 3', { exact: false }).count(), 0, 'no text names the old step numbers')

  // Position 2: two steps one below the other, each as wide as the card; step 1 has the same head as step 2;
  // the ways of measuring with the tick "With mesh", the choice and the start row follow in this order.
  const s2 = await box(sec2)
  assert.ok(s2.y >= s1.y + s1.height - 1, 'step 2 is under step 1')
  assert.ok(Math.abs(s1.width - s2.width) < 2, 'the steps are as wide as the card')
  assert.equal(await card.locator('[data-level-step="3"]').count(), 0, 'there are two steps: step 3 is dissolved into step 2')
  const styleOf = loc => loc.evaluate(e => { const c = getComputedStyle(e); return [c.paddingLeft, c.paddingTop, c.paddingRight, c.paddingBottom].join(' ') })
  assert.equal(await styleOf(sec1.locator('> div').first()), await styleOf(sec2.locator('> button')), 'step 1 has the same head padding as step 2')
  const badge1 = await box(sec1.locator('[data-num-mark]').first())
  const badge2 = await box(sec2.locator('> button [data-num-mark]'))
  assert.equal(Math.round(badge1.x - s1.x), Math.round(badge2.x - s2.x), 'the same inner spacing at the left')
  const modeBox = await box(sec1.locator('[data-level-start-mode]'))
  const selectBox = await box(sec1.locator('[data-level-base]'))
  const startBox = await box(sec1.locator('[data-level-start]'))
  assert.equal(await sec1.locator('[data-level-age]').count(), 0, 'without the tick no note about a mesh')
  assert.ok(modeBox.y + modeBox.height <= selectBox.y && selectBox.y + selectBox.height <= startBox.y, 'ways, choice, start row: in this order')
  // The head row: the two ways, the tick "With mesh" right beside them, then the probing choice, on one row.
  const tickBox = await box(sec1.locator('[data-level-mesh-toggle]'))
  const twiceBox = await box(sec1.locator('[data-level-probe-twice]'))
  assert.ok(tickBox.x >= modeBox.x + modeBox.width && twiceBox.x >= tickBox.x + tickBox.width - 1, 'ways, tick, probing: from left to right')
  assert.ok(tickBox.y < modeBox.y + modeBox.height && twiceBox.y < modeBox.y + modeBox.height, 'on the same row')

  // Position 3, step 2: the bed picture is left, the lists and the table right of it, the help under the table; the picture
  // keeps its height (and the right column stays beside it) when the help opens.
  const picture = sec2.locator('.min-h-80').first()
  const lists = await box(card.locator('[data-screw-lists]'))
  const pic = await box(picture)
  // The place on the page, not in the window: the click on the help may scroll the page.
  const pageTop = loc => loc.evaluate(e => Math.round(e.getBoundingClientRect().top + scrollY))
  const picTop = await pageTop(picture)
  const tableBox = await box(sec2.locator('[data-screw-proposal-block] table'))
  assert.ok(lists.x >= pic.x + pic.width, 'the lists are in the right column')
  const help3 = card.locator('details[data-screw-help]')
  const helpBox = await box(help3)
  assert.ok(helpBox.y >= tableBox.y + tableBox.height - 1, 'the help is under the table')
  assert.ok(helpBox.x >= pic.x + pic.width, 'the help is in the right column')
  await help3.locator('summary').click()
  const picOpen = await box(picture)
  assert.equal(Math.round(picOpen.height), Math.round(pic.height), 'the bed picture keeps its height when the help opens')
  assert.equal(await pageTop(picture), picTop, 'the bed picture keeps its place on the page')
  await help3.locator('summary').click()
  // Back to the top, where the boxes above were measured.
  await page.evaluate(() => scrollTo(0, 0))

  // Point 3: before a session step 2 has no action buttons (Quick: none; "End session" is only in the session bar); the
  // earlier sessions come last, outside any step.
  assert.equal(await check.count(), 0, 'no "Measure again" before a session')
  assert.equal(await sec2.getByRole('button', { name: /End session/ }).count(), 0, 'step 2 has no End session')
  assert.equal(await sec2.getByRole('button', { name: /Neue Messung|Measure again with a session/ }).count(), 0, 'no new-measurement button')
  const earlier = await box(card.locator('#screw-history summary'))
  assert.ok(earlier.y >= s2.y + s2.height - 1, 'the earlier sessions come last, below step 2')
  assert.equal(await card.locator('#screw-history').evaluate(el => Boolean(el.closest('[data-level-step]'))), false, 'the earlier sessions are no step')
  // The help and "Create new mesh" share one row that does not make step 2 taller than the table needs.
  const row = await box(sec2.locator('[data-screw-help-row]'))
  const newMesh = await box(sec2.locator('[data-level-new-mesh-open]'))
  const helpClosed = await box(sec2.locator('details[data-screw-help]'))
  assert.ok(Math.abs(newMesh.y - helpClosed.y) < 12, 'help and new mesh are on one row')
  assert.ok(newMesh.x > helpClosed.x, 'arrangement A: the help left, the button right')
  // Opening the help moves the button nowhere (measured on the page, not in the window: the click may scroll the summary into view).
  const onPage = loc => loc.evaluate(e => ({ x: Math.round(e.getBoundingClientRect().left + scrollX), y: Math.round(e.getBoundingClientRect().top + scrollY) }))
  const newMeshPage = await onPage(sec2.locator('[data-level-new-mesh-open]'))
  await sec2.locator('details[data-screw-help] summary').click()
  const newMeshOpen = await onPage(sec2.locator('[data-level-new-mesh-open]'))
  assert.ok(Math.abs(newMeshOpen.x - newMeshPage.x) < 2 && Math.abs(newMeshOpen.y - newMeshPage.y) < 2, 'the button stays in place when the help opens')
  {
    // Opened, the help text takes the full width under the row; the summary keeps its own place too.
    const newMeshView = await box(sec2.locator('[data-level-new-mesh-open]'))
    const body = await box(sec2.locator('[data-screw-help-body]'))
    const summaryOpen = await box(sec2.locator('details[data-screw-help]'))
    assert.ok(Math.abs(summaryOpen.x - helpClosed.x) < 2 && Math.abs(summaryOpen.width - helpClosed.width) < 2, 'the help title keeps its place and width')
    assert.ok(body.y >= newMeshView.y + newMeshView.height - 1, 'the help text stands below the row')
    assert.ok(body.x + body.width >= newMeshView.x + newMeshView.width - 2, 'the help text reaches under the button: full width')
  }
  await sec2.locator('details[data-screw-help] summary').click()

  await page.screenshot({ path: path.join(shots, 'screw-actions-idle-1440.png'), fullPage: true })
  // Quick measurement is blocked during a session and says why.
  await mode('session')
  const face = loc => loc.evaluate(e => { const c = getComputedStyle(e); return `${c.backgroundColor}|${c.color}|${c.boxShadow}` })
  const startEnabledFace = await face(start)
  await start.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('Session active', { exact: true }).first().waitFor()
  // In the session: "Measure again" stands ABOVE mode, height and turn; "End session" is only in the session bar.
  const checkBox = await box(check)
  const listsBox = await box(card.locator('[data-screw-lists]'))
  assert.ok(checkBox.y + checkBox.height <= listsBox.y + 1, 'Measure again is above the mode, height and turn choices')
  assert.equal(await sec2.getByRole('button', { name: /End session/ }).count(), 0, 'no End session in step 2')
  // The button is as wide as its text, not as the rest of the column; the corner buttons are named "Single measurement".
  assert.equal(await check.innerText(), 'Measure again')
  assert.ok(checkBox.width < 220, `the button is only as wide as its text (${Math.round(checkBox.width)} px)`)
  assert.equal(await card.getByRole('button', { name: /^Single measurement: / }).count() > 0, true, 'corner buttons say Single measurement')
  assert.equal(await card.locator('[aria-label^="Single measurement"]').first().innerText(), 'Single measurement')
  await page.locator('[data-level-session-bar]').getByRole('button', { name: /End session/ }).waitFor()
  // The (i) of the click unit has a reserved place: choosing the click unit moves nothing.
  const slotBefore = await box(card.locator('[data-screw-hint-slot]'))
  const unitBefore = await box(card.locator('[data-screw-turn-unit] select'))
  await card.locator('[data-screw-turn-unit] select').selectOption('clicks')
  await card.locator('[data-hint="clicks"]').waitFor()
  const slotAfter = await box(card.locator('[data-screw-hint-slot]'))
  const unitAfter = await box(card.locator('[data-screw-turn-unit] select'))
  assert.deepEqual([slotAfter.x, slotAfter.y, unitAfter.x, unitAfter.width], [slotBefore.x, slotBefore.y, unitBefore.x, unitBefore.width], 'the (i) appears in its reserved place and moves nothing')
  assert.ok(slotAfter.x >= unitAfter.x + unitAfter.width - 1, 'the (i) is right of the select')
  await card.locator('[data-screw-turn-unit] select').selectOption('degrees')
  // The quick way cannot be chosen during a session (the choice moved on to the session); the tick "With mesh" still can.
  const quickMode = card.locator('[data-level-start-mode-button="quick"]')
  assert.ok(await quickMode.isDisabled(), 'the quick mode is locked during a session')
  assert.equal(await quickMode.getAttribute('aria-pressed'), 'false')
  assert.equal(await card.locator('[data-level-start-mode-button="session"]').getAttribute('aria-pressed'), 'true')
  // The basis of a session is fixed when it starts: the tick "With mesh" cannot be set or cleared during a session (owner's decision 20 Nov).
  assert.ok(await card.locator('[data-level-mesh-toggle] input').isDisabled(), 'the tick is locked during a session')
  assert.equal(await quick.count(), 0, 'no quick start button during a session')
  assert.ok(await start.isDisabled(), 'no second session start during a session')
  // A locked main action looks locked: the dark greyed-out face, not the accent colour.
  const startLockedFace = await face(start)
  assert.notEqual(startLockedFace, startEnabledFace, 'the locked start has another face than the free one')
  assert.ok(!/rgb\(\s*20,\s*207,\s*233/.test(startLockedFace.split('|')[0]), `the locked start is not accent-coloured (${startLockedFace})`)
  assert.equal(startLockedFace.split('|')[2].replaceAll('rgba(0, 0, 0, 0) 0px 0px 0px 0px', '').replace(/[,\s]/g, '').replace('none', ''), '', 'no glow on a locked main action')
  await card.locator('[data-screw-quick-hint]').getByText('Unavailable while a session is running.').waitFor()

  // Point 4: while a measurement runs the page says what it contains.
  let busy = false
  const fulfill = body => ({ contentType: 'application/json', body: JSON.stringify(body) })
  await page.route(/\/api\/console(\?.*)?$/, route => {
    if (!busy || route.request().method() !== 'GET') return route.fallback()
    // A screw measurement, as the console names it (the tab shows the line only for one).
    return route.fulfill(fulfill({ busy: true, completed: false, success: false, generation: 90, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n', output: '' }))
  })
  const status = card.locator('[data-screw-status]')
  assert.equal(await status.count(), 0, 'no status while nothing runs')
  await page.reload()
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  await page.getByText('Session active', { exact: true }).first().waitFor()
  busy = true
  // Started elsewhere or before a reload: the page does not know the kind and shows no counter.
  await status.getByText(/^Measurement running\.$/).waitFor({ timeout: 8000 })
  assert.equal(await status.getByText(/running for/).count(), 0, 'no counter without a known start')
  // Started here: the kind and the seconds are shown, the corner button says it measures.
  busy = false
  await status.waitFor({ state: 'detached', timeout: 8000 })
  page.on('request', r => {
    if (r.method() === 'POST' && r.url().endsWith('/api/control')) busy = true
  })
  const fl = card.getByRole('button', { name: /Single measurement: Front left/ })
  await page.waitForFunction(() => {
    const b = [...document.querySelectorAll('button')].find(x => x.getAttribute('aria-label') === 'Single measurement: Front left')
    return b && !b.disabled
  })
  await fl.click()
  await status.getByText(/^Measurement running for \d+ s\.$/).waitFor({ timeout: 8000 })
  await card.locator('[data-screw-running="FL"]').getByText('Measuring…').waitFor()
  await page.screenshot({ path: path.join(shots, 'screw-actions-running-1440.png'), fullPage: true })
  busy = false
  await status.waitFor({ state: 'detached', timeout: 8000 })
  // Point 5: "Measured at" is the actual temperature of the last measurement, for bed, nozzle and chamber;
  // without a measurement there is no value. The setpoint is called "Target".
  const temps = card.locator('#screw-temp-values')
  await temps.locator('[data-temp-row="bed"] [data-value="target"]').getByText('Target 60 °C', { exact: true }).waitFor() // during the session the row names the target
  assert.equal(await temps.locator('[data-value="measured"]').count(), 0, 'no measurement with temperatures: no "Measured at"')
  let ist = false
  await page.route('**/api/screws/history', route => {
    if (!ist || route.request().method() !== 'GET') return route.fallback()
    return route.fulfill(
      fulfill({
        sessions: [
          {
            kind: 'quick',
            started: 100,
            ended: 160,
            measurements: [
              { time: 160, kind: 'quick', corner: null, twice: false, z: { FL: -0.7, FR: -0.6, RR: -0.8, RL: -0.75 }, bed: 60, ist: { bed: 60.4, nozzle: 140, chamber: 22 } },
            ],
          },
          {
            kind: 'session',
            started: 10,
            ended: null,
            measurements: [
              { time: 20, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 }, bed: 60, ist: { bed: 61.7, nozzle: 139.8, chamber: 23 } },
              { time: 30, kind: 'probe', corner: 'FR', twice: false, z: { FR: 1.75 }, bed: 60, ist: { bed: 62.1, nozzle: 140.2 } },
            ],
          },
        ],
      })
    )
  })
  ist = true
  const bedRow = temps.locator('[data-temp-row="bed"]')
  await bedRow.locator('[data-value="measured"]').getByText('Measured at 62.1 °C', { exact: true }).waitFor({ timeout: 8000 })
  await temps.locator('[data-temp-row="nozzle"] [data-value="measured"]').getByText('Measured at 140.2 °C', { exact: true }).waitFor()
  assert.equal(await temps.locator('[data-temp-row="chamber"] [data-value="measured"]').count(), 0, 'the newest measurement had no chamber value: none shown')
  await card.locator('#screw-history summary').first().click()
  const quickRow = card.locator('[data-screw-kind="quick"]')
  await quickRow.locator('[data-col="bed"]').getByText('60.4', { exact: true }).waitFor()
  await quickRow.locator('[data-col="nozzle"]').getByText('140.0', { exact: true }).waitFor()
  await quickRow.locator('[data-col="chamber"]').getByText('22', { exact: true }).waitFor()
  ist = false
  await page.setViewportSize({ width: 390, height: 844 })
  await page.reload() // a phone loads the page at its width: the side menu starts closed
  await card.waitFor()
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no horizontal overflow at 390 px')
  // On a phone the ways of measuring wrap under the title of step 1.
  const head390 = card.locator('[data-level-step="1"] > div').first()
  const title390 = await box(head390.locator('h3'))
  const ways390 = await box(card.locator('[data-level-step="1"] [data-level-start-mode]'))
  assert.ok(ways390.y >= title390.y + title390.height - 1, 'on a phone the ways are under the title')
  assert.ok((await box(card.locator('[data-level-step="2"]'))).y >= (await box(card.locator('[data-level-step="1"]'))).y + (await box(card.locator('[data-level-step="1"]'))).height - 1, 'on a phone step 2 is under step 1')
  await page.screenshot({ path: path.join(shots, 'screw-actions-390.png'), fullPage: true })
  console.log('checks done')
} catch (error) {
  console.error(error)
  process.exitCode = 1
} finally {
  await browser?.close()
  await server.close()
}
if (errors.length) {
  console.error(errors)
  process.exitCode = 1
}
if (!process.exitCode) console.log('PASS screw page actions')
