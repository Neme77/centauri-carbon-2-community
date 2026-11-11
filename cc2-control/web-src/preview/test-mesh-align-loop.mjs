// Mesh-guided screw alignment, the guided loop: every step lists its printer commands and sends them only
// after the confirmation; a declined, refused or failed step, an ended session, a print that starts
// meanwhile and used-up rounds are shown for what they are. The printer is the demo's simulation and the
// meshes are synthetic.
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { analyseMesh } from '../src/lib/mesh-align.ts'
import { asProfileReply, dome, syntheticMesh } from './mesh-align-synthetic.mjs'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
const number = (value, digits) =>
  new Intl.NumberFormat('en', { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(Math.abs(value))
const signed = value => `${value > 0 ? '+' : value < 0 ? '−' : ''}${number(value, 3)}`
let browser
const errors = []
// Every request to the session, with its body: a body ending in CONFIRM is the only one that acts.
const sent = []
const confirmed = () => sent.filter(r => /(^|\n)CONFIRM:[0-9a-f]{8}$/.test(r.body))
const shots = path.join(root, 'preview/artifacts')
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
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    const at = new URL(r.url()).pathname
    if (r.method() === 'POST' && at.startsWith('/api/screws/align/')) sent.push({ path: at.slice(18), body: r.postData() || '' })
  })
  let mesh = null
  await page.route('**/api/mesh', route => (mesh ? route.fulfill({ json: mesh }) : route.fallback()))
  // The round limit below is changed in the settings block, an experimental opt-in.
  await page.route('**/api/experiments', route =>
    route.fulfill({
      json: { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, mesh_align_settings: true },
    })
  )
  // One confirmed request can be refused the way the backend refuses it when the console is busy.
  let refuse = 0
  await page.route('**/api/screws/align/measure', route => {
    if (refuse && /\nCONFIRM:[0-9a-f]{8}$/.test(route.request().postData() || '')) {
      refuse--
      return route.fulfill({ status: 409, json: { ok: false, error: 'Another printer command is still running' } })
    }
    return route.fallback()
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const open = async () => {
    await page.goto(`${origin}/#bed/align`)
    await page.locator('#cc2-preview-banner').waitFor()
  }
  await open()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  const card = page.locator('#meshAlign')
  const guide = card.locator('[data-align="guide"]')
  const part = name => card.locator(`[data-align="${name}"]`)
  const dialog = page.getByRole('dialog')
  const confirm = () => dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  const cancel = () => dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
  const button = name => guide.getByRole('button', { name, exact: true })
  const state = () => page.evaluate(async () => (await fetch('/api/screws/align')).json())
  const step = (body = '') =>
    page.evaluate(b => fetch('/__preview/calibration-step', { method: 'POST', headers: { 'X-CC2-Request': '1' }, body: b }), body)
  const toast = text => page.getByText(text, { exact: true }).first().waitFor()
  // The simulated calibration of a round, up to the mesh the firmware saved.
  const finishRound = async grid => {
    await part('progress').getByText('Heating the bed: 25 °C of 60 °C').waitFor()
    await step()
    await part('progress').getByText(/Holding the bed at 60 °C: \d+:\d\d left/).waitFor()
    await step()
    await part('progress').getByText('Probing Side A at 60 °C…').waitFor()
    await part('progress').getByText('It can now only be interrupted with the emergency stop.').waitFor()
    assert.ok(await button('End the session').isDisabled(), 'a running probe cannot be ended from the page')
    mesh = asProfileReply('default', grid)
    await step()
    await part('progress').waitFor({ state: 'detached' })
  }

  // No session yet: the loop explains itself and offers the first measurement.
  await guide.getByRole('heading', { name: 'Guided alignment', exact: true }).waitFor()
  await guide.getByText('every round runs the full bed mesh calibration (BED_MESH_CALIBRATE)').waitFor()
  await guide.getByText('sends them only after you confirm').waitFor()
  assert.equal(await guide.getAttribute('data-align-active'), 'no')
  assert.equal(await button('End the session').count(), 0)
  assert.equal((await state()).active, false)

  // The first step lists its commands. Declined, nothing was confirmed and no session exists.
  await button('Measure the mesh').click()
  await dialog.getByText('Run measurement 1 of 4?').waitFor()
  const listed = await dialog.innerText()
  assert.match(listed, /M140 S60 — heat the bed; while this target stays set it is renewed every ten minutes until the session ends/)
  assert.match(listed, /M104 S0 — switch the nozzle off after the measurement/)
  assert.match(listed, /\(then CC2 Control waits until the bed reads 60 °C and holds it for 2 min; nothing is sent meanwhile\)/)
  assert.match(listed, /BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60 — probe the full mesh; it replaces the mesh of this side in the printer/)
  assert.match(listed, /No shorter probing command is confirmed for this firmware/)
  assert.ok(listed.indexOf('M140 S60') < listed.indexOf('BED_MESH_CALIBRATE PROFILE'), 'commands are listed in the order they are sent')
  await cancel()
  await dialog.waitFor({ state: 'detached' })
  assert.equal(sent.length, 1, 'only the listing request left the page')
  assert.equal(confirmed().length, 0, 'a declined confirmation sends no command')
  assert.equal((await state()).active, false)
  assert.equal((await page.evaluate(async () => (await fetch('/api/plates')).json())).calibration.stage, 'off')

  // Confirmed: exactly the listed request goes out, once, and the round runs on the printer.
  const tiltedGrid = syntheticMesh({ tiltX: 0.4, tiltY: -0.1, shape: dome(0.2) })
  const tilted = analyseMesh(tiltedGrid)
  await button('Measure the mesh').click()
  await dialog.getByText('Run measurement 1 of 4?').waitFor()
  await confirm()
  await part('round').getByText('Round 1 of 4', { exact: true }).waitFor()
  assert.equal(confirmed().length, 1)
  assert.match(confirmed()[0].body, /^A\n60\n2\n[0-9a-f]*\n4\nCONFIRM:[0-9a-f]{8}$/)
  assert.equal(await guide.getAttribute('data-align-active'), 'yes')
  assert.ok(await button('Measure again').isDisabled(), 'no second round while one runs')
  await finishRound(tiltedGrid)
  await part('rounds')
    .getByText(`Round 1: tilt X ${signed(tilted.tilt_x)} mm, tilt Y ${signed(tilted.tilt_y)} mm`, { exact: true })
    .waitFor()
  await part('advice').getByText('Turn the screws as listed above, then measure again.').waitFor()
  assert.equal(await part('trend').count(), 0, 'one round has nothing to compare with')
  // The card now shows the mesh just measured and what each screw has to do.
  assert.equal(await part('source').inputValue(), 'default')
  await part('tilt-x').getByText(`${signed(tilted.tilt_x)} mm`).waitFor()
  await part('verdict').getByText('Not aligned yet').waitFor()
  const fr = tilted.screws.find(s => s.id === 'FR')
  if (!fr.reference && Math.abs(fr.correction) >= 0.005)
    await card
      .locator('[data-align-screw="FR"]')
      .getByText(`${fr.correction > 0 ? 'Raise' : 'Lower'} the bed at this corner by ${number(fr.correction, 2)} mm`, { exact: true })
      .waitFor()
  assert.deepEqual(
    (({ active, round, rounds, measuring, measured }) => ({ active, round, rounds, measuring, measured }))(await state()),
    { active: true, round: 1, rounds: 4, measuring: false, measured: true }
  )
  await page.screenshot({ path: path.join(shots, 'mesh-align-loop-round-1440.png'), fullPage: true })

  // A refused command is reported and not repeated: the session waits where it was.
  refuse = 1
  await button('Measure again').click()
  await dialog.getByText('Run measurement 2 of 4?').waitFor()
  await confirm()
  await toast('Rejected: Another printer command is still running')
  assert.equal(confirmed().length, 2, 'the refused request was sent once')
  await page.waitForTimeout(2500) // a poll passes: nothing is retried behind the user's back
  assert.equal(confirmed().length, 2)
  assert.deepEqual((({ round, measuring }) => ({ round, measuring }))(await state()), { round: 1, measuring: false })
  await part('round').getByText('Round 1 of 4', { exact: true }).waitFor()

  // The second round measures a level bed: the tilt got smaller and the loop asks for the save.
  const levelGrid = syntheticMesh({ tiltX: 0.02, tiltY: -0.01, shape: dome(0.2) })
  const level = analyseMesh(levelGrid)
  assert.equal(level.aligned, true)
  await button('Measure again').click()
  await dialog.getByText('Run measurement 2 of 4?').waitFor()
  await confirm()
  await part('round').getByText('Round 2 of 4', { exact: true }).waitFor()
  await finishRound(levelGrid)
  await part('rounds').getByText(`Round 2: tilt X ${signed(level.tilt_x)} mm, tilt Y ${signed(level.tilt_y)} mm`, { exact: true }).waitFor()
  assert.equal(await part('rounds').locator('li').count(), 2)
  await part('trend').getByText('The tilt got smaller since the last round.').waitFor()
  await part('advice').getByText('Both tilts are below the target. Save the mesh to finish.').waitFor()
  await part('verdict').getByText('Alignment done, the rest is plate shape').waitFor()

  // The save lists what the plate library sends; declined, nothing is saved.
  const before = await page.evaluate(async () => (await fetch('/api/plates')).json())
  const target = before.plates.find(p => p.id === before.current)
  await button('Save the mesh and finish').click()
  await dialog.getByText(`Save this mesh as the 60 °C measurement of ${target.name} and end the session?`).waitFor()
  const saving = await dialog.innerText()
  assert.match(saving, /BED_MESH_PROFILE LOAD=default — load the mesh of this side/)
  assert.match(saving, /BED_MESH_PROFILE SAVE=cc2_[0-9a-f]{16} — store it in the printer as the profile of a plate measurement/)
  assert.match(saving, /SET_GCODE_OFFSET Z=-0\.020 — apply the Z offset of the plate; nothing moves/)
  assert.match(saving, /TURN_OFF_HEATERS — switch both heaters off, as soon as the printer is idle/)
  await cancel()
  await dialog.waitFor({ state: 'detached' })
  assert.equal(confirmed().length, 3)
  assert.equal((await state()).active, true)
  await button('Save the mesh and finish').click()
  await confirm()
  await toast(`Saved to “${target.name}” as its 60 °C measurement.`)
  assert.equal(confirmed().length, 4)
  assert.match(confirmed()[3].body, new RegExp(`^${target.id}\\nCONFIRM:[0-9a-f]{8}$`))
  await part('ended').getByText('The mesh was saved and the session ended.').waitFor()
  await guide.getByText('The heaters are switched off as soon as the printer is idle.').waitFor()
  assert.equal(await guide.getAttribute('data-align-active'), 'no')
  assert.deepEqual((({ active, ended }) => ({ active, ended }))(await state()), { active: false, ended: 'saved' })
  const after = await page.evaluate(async () => (await fetch('/api/plates')).json())
  assert.equal(after.plates.find(p => p.id === target.id).measures.find(m => m.temp === 60).measured, 1790990000)

  // Ending a session while the bed heats: the list comes first; declined, the measurement goes on.
  await button('Measure the mesh').click()
  await confirm()
  await part('progress').getByText('Heating the bed: 25 °C of 60 °C').waitFor()
  assert.equal(await part('rounds').count(), 0, 'a new session starts without the old rounds')
  await button('End the session').click()
  await dialog.getByText('End the alignment session?').waitFor()
  const ending = await dialog.innerText()
  assert.match(ending, /M140 S0 — switch the bed heater off/)
  assert.match(ending, /TURN_OFF_HEATERS — switch both heaters off, as soon as the printer is idle/)
  assert.match(ending, /run a bed mesh calibration before printing/)
  await cancel()
  await dialog.waitFor({ state: 'detached' })
  assert.deepEqual((({ active, measuring }) => ({ active, measuring }))(await state()), { active: true, measuring: true })
  await button('End the session').click()
  await confirm()
  await part('ended').getByText('The session was ended.').waitFor()
  await part('ended').getByText('run a bed mesh calibration before printing').waitFor()
  assert.deepEqual((({ active, ended }) => ({ active, ended }))(await state()), { active: false, ended: 'ended' })
  const stopped = (await page.evaluate(async () => (await fetch('/api/plates')).json())).calibration
  assert.deepEqual([stopped.stage, stopped.result], ['off', 'cancelled'], 'the running calibration was stopped')

  // A measurement that fails ends the session and says why.
  await button('Measure the mesh').click()
  await confirm()
  await part('progress').getByText('Heating the bed: 25 °C of 60 °C').waitFor()
  await step('fail')
  await part('ended').getByText('The measurement failed and the session ended: The bed did not hold the temperature, so the calibration was stopped.').waitFor()
  await guide.getByText('The heaters are switched off as soon as the printer is idle.').waitFor()
  assert.equal(await button('End the session').count(), 0)
  assert.equal((await state()).ended, 'failed')

  // The rounds are limited: with one round and a bed that is still tilted, only saving or ending is left.
  const settings = part('settings')
  await settings.getByText('Alignment settings', { exact: true }).click()
  await settings.getByLabel('Maximum rounds (estimate)', { exact: true }).fill('1')
  await settings.getByLabel('Maximum rounds (estimate)', { exact: true }).blur()
  await button('Measure the mesh').click()
  await dialog.getByText('Run measurement 1 of 1?').waitFor()
  await confirm()
  await finishRound(tiltedGrid)
  await part('advice').getByText('All 1 rounds are used and a tilt is still above the target. Save this mesh or end the session.').waitFor()
  assert.ok(await button('Measure again').isDisabled())
  assert.ok(await button('Save the mesh and finish').isEnabled())
  assert.match(confirmed().at(-1).body, /\n1\nCONFIRM:[0-9a-f]{8}$/)

  // A print that starts between two rounds ends the session and keeps its heat.
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('printing')])
  await open()
  await part('ended').getByText('A print started, so the session ended. The heaters were left to the print.').waitFor()
  assert.equal(await guide.getByText('The heaters are switched off').count(), 0)
  assert.deepEqual(
    (({ active, ended, heaters_off_pending }) => ({ active, ended, heaters_off_pending }))(await state()),
    { active: false, ended: 'print', heaters_off_pending: false }
  )
  // While it prints a new session is refused by the printer, not started by the page.
  const refusals = confirmed().length
  await button('Measure the mesh').click()
  await toast('Rejected: The printer must be idle')
  assert.equal(confirmed().length, refusals)
  assert.equal(await dialog.count(), 0)

  // A session that ended because the page went away says so when the page is back.
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.route('**/api/screws/align', async route => {
    const data = await (await route.fetch()).json()
    return route.fulfill({ json: { ...data, active: false, ended: 'timeout', heaters_off_pending: true } })
  })
  await open()
  await part('ended').getByText('The session ended because nothing was requested for 20 minutes.').waitFor()
  await guide.getByText('The heaters are switched off as soon as the printer is idle.').waitFor()

  // Phone width: nothing overflows.
  await page.setViewportSize({ width: 390, height: 844 })
  await guide.scrollIntoViewIfNeeded()
  assert.ok(
    await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1),
    'mobile must not overflow'
  )
  await page.screenshot({ path: path.join(shots, 'mesh-align-loop-390.png'), fullPage: true })

  // An older CC2 Control without the session: the analysis stays, the loop is not offered.
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.unroute('**/api/screws/align')
  await page.route('**/api/screws/align', route => route.fulfill({ status: 404, json: { error: 'Not found' } }))
  const unsupported = page.waitForResponse(r => r.url().endsWith('/api/screws/align') && r.status() === 404)
  await page.reload()
  await unsupported
  await part('tilt-x').waitFor()
  assert.equal(await guide.count(), 0)

  assert.deepEqual(errors, [])
  console.log('PASS mesh-guided screw alignment loop: commands listed before they are sent, declined, refused and failed steps, save, end, round limit, a print meanwhile, timeout note')
} finally {
  await browser?.close()
  await server.close()
}
