// Quick measurement of one screw after a quick measurement: a button per corner with a warning, the result merged
// into the last full quick measurement, front left as the reference (a shift above 10 µm is warned about),
// and a way on into a session. Not offered during a session.
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { mergeCornerMeasure } from '../src/lib/screws.ts'

const lines = heights =>
  Object.entries(heights)
    .map(([key, z]) => {
      const [x, y] = key.split(',')
      const h = Number(z).toFixed(6)
      return `${`// probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${h}\n`.repeat(3)}// Result is z=${h}\n`
    })
    .join('')
// The merge: front left and one corner measured; the others keep their differences against front left.
{
  const base = { '27.9,25.95': -0.7, '227.15,25.95': -0.62, '227.15,225.5': -0.8, '27.9,225.5': -0.75 }
  const m = mergeCornerMeasure(base, lines({ '27.9,25.95': -0.68, '227.15,25.95': -0.55 }), 'FR')
  assert.equal(Math.round(m.shift * 1000), 20)
  assert.equal(Math.round((m.values['227.15,25.95'] - m.values['27.9,25.95']) * 1000), 130, 'the re-measured corner uses its new difference')
  assert.equal(Math.round((m.values['227.15,225.5'] - m.values['27.9,25.95']) * 1000), -100, 'the other corners keep their differences')
  assert.equal(Math.round((m.values['27.9,225.5'] - m.values['27.9,25.95']) * 1000), -50)
  assert.equal(mergeCornerMeasure(base, lines({ '27.9,25.95': -0.68 }), 'FR'), null, 'without the corner result there is nothing to merge')
  assert.equal(mergeCornerMeasure(base, lines({ '227.15,25.95': -0.55 }), 'FR'), null, 'without front left there is nothing to merge')
  assert.equal(mergeCornerMeasure({ '27.9,25.95': 1 }, lines({ '27.9,25.95': 1, '227.15,25.95': 1 }), 'FR'), null, 'without a full base there is nothing to merge')
  assert.ok(mergeCornerMeasure(base, lines({ '27.9,25.95': -0.69 }), 'FL'), 'front left alone only moves the frame')
}

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
const shots = path.join(root, 'preview/artifacts')
const errors = []
let browser
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
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } })
  await page.addInitScript(() => { try { localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' })); localStorage.setItem('cc2-screw-turn-mode', 'exact') } catch {} })
  page.on('pageerror', e => errors.push(e.message))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  let consoleJob = { busy: false, completed: true, success: true, generation: 50, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${lines({ '27.9,25.95': -0.7, '227.15,25.95': -0.62, '227.15,225.5': -0.8, '27.9,225.5': -0.75 })}` }
  const initialJob = consoleJob
  const posted = []
  let next = null
  await page.route(/\/api\/console(\?.*)?$/, route => (route.request().method() === 'GET' ? route.fulfill({ json: consoleJob }) : route.fallback()))
  await page.route('**/api/control', route => {
    if (route.request().method() !== 'POST') return route.fallback()
    const body = route.request().postData()
    if (!body?.startsWith('screws:measure')) return route.fallback()
    posted.push(body)
    if (next) consoleJob = next
    return route.fulfill({ json: { accepted: true } })
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.goto(`${origin}/#bed/screws`)
  const card = page.locator('#screwFocus')
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  // The hold time of step 1 goes in front of every start the user makes; 0 here (test-bed-mesh-tick covers it).
  await card.locator('[data-level-soak]').selectOption('0')
  const corner = id => card.locator(`[data-screw-quick-corner="${id}"]`)
  await corner('FR').waitFor({ timeout: 8000 })
  assert.equal(await corner('FR').innerText(), 'Single measurement (quick)')
  assert.ok((await corner('FR').isEnabled()) && (await corner('RR').isEnabled()) && (await corner('RL').isEnabled()))
  assert.equal(await corner('FL').count(), 0, 'front left is the reference: no quick re-measurement of it')
  assert.ok(await card.getByRole('button', { name: 'Single measurement: Front left', exact: true }).isDisabled())
  // No button for a new measurement with a session any more: the user chooses "With session" at the top.
  assert.equal(await card.locator('[data-screw-quick-continue]').count(), 0, 'no "Measure again with a session" button')
  assert.equal(await card.getByText('Measure again with a session').count(), 0)
  assert.equal(await card.locator('[data-screw-quick-corner-hint]').count(), 0, 'X and Y referenced: no extra text')
  await page.screenshot({ path: path.join(shots, 'screw-corner-quick-1440.png'), fullPage: true })

  // The warning names what happens; cancelling sends nothing.
  await corner('FR').click()
  const dialog = page.getByRole('dialog')
  // X and Y are referenced (the idle scene reports xyz): only Z is homed, after a 30 s wait.
  await dialog.getByText(/X and Y are still referenced, so the printer homes only Z and heats the bed to 60 °C/).waitFor()
  await dialog.getByText(/After a 30 s wait it measures this corner and front left as the reference/).waitFor()
  await dialog.getByText(/both heaters turn off/).waitFor()
  assert.equal(await dialog.getByText(/Accuracy compared with a session/).count(), 0, 'no placeholder sentence without a measured accuracy')
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.deepEqual(posted, [], 'cancel starts nothing')

  // Confirm: front left moved by +20 µm, the corner has a new value; the other corners keep their differences.
  next = { busy: false, completed: true, success: true, generation: 51, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${lines({ '27.9,25.95': -0.68, '227.15,25.95': -0.55 })}` }
  await corner('FR').click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await card.locator('[data-screw-quick-shift]').getByText('The front-left reference moved by +20 µm. The comparison with the other corners is uncertain.').waitFor({ timeout: 8000 })
  assert.deepEqual(posted, ['screws:measure:corner:FR:60:2'], 'the corner action is sent, with 2 × 3 probes (the default)')
  const ROW_IDS = { 'Front left': 'FL', 'Front right': 'FR', 'Back right': 'RR', 'Back left': 'RL', 'Vorne links': 'FL', 'Vorne rechts': 'FR', 'Hinten rechts': 'RR', 'Hinten links': 'RL' }
  // The row of a corner (its first cell also holds the number button 1–4).
  const row = name => card.locator(`tbody tr[data-screw-row="${ROW_IDS[name]}"]`)
  await row('Front right').locator('[data-screw-exact]').getByText('66.9°', { exact: true }).waitFor()
  await row('Back right').locator('[data-screw-exact]').getByText('51.4°', { exact: true }).waitFor()
  await row('Back left').locator('[data-screw-exact]').getByText('25.7°', { exact: true }).waitFor()
  await page.screenshot({ path: path.join(shots, 'screw-corner-shift-1440.png'), fullPage: true })

  // A small shift (10 µm or less) shows no warning.
  next = { ...next, generation: 52, output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${lines({ '27.9,25.95': -0.671, '227.15,225.5': -0.77 })}` }
  await corner('RR').click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await row('Back right').locator('[data-screw-exact]').getByText('50.9°', { exact: true }).waitFor()
  await card.locator('[data-screw-quick-shift]').waitFor({ state: 'detached', timeout: 8000 })
  assert.deepEqual(posted, ['screws:measure:corner:FR:60:2', 'screws:measure:corner:RR:60:2'])
  // Without "2× 3 probes" the plain action goes out.
  await card.locator('[data-level-probe-twice] input').uncheck()
  await corner('RL').click()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForTimeout(300)
  assert.equal(posted.at(-1), 'screws:measure:corner:RL', 'once: the plain corner action')
  await card.locator('[data-level-probe-twice] input').check()

  // X and Y not referenced: the full preparation is announced.
  consoleJob = initialJob
  next = null
  await page.route('**/api/printer', async route => {
    const response = await route.fetch()
    const body = await response.json()
    body.motion = { ...body.motion, homed_axes: '' }
    await route.fulfill({ response, json: body })
  })
  await page.reload()
  await card.waitFor()
  await corner('FR').waitFor({ timeout: 8000 })
  await card.locator('[data-screw-quick-corner-hint]').getByText('X and Y are not referenced: measuring a corner again first homes all axes and cleans the nozzle, which takes longer.').waitFor()
  await corner('FR').click()
  await dialog.getByText(/Quick measurement of this screw: the printer homes, cleans the nozzle and heats the bed to 60 °C/).waitFor()
  await dialog.getByText(/almost as long as a whole quick measurement/).waitFor()
  await page.getByRole('button', { name: 'Cancel', exact: true }).click()
  await page.unroute('**/api/printer')
  await page.reload()
  await card.waitFor()
  next = null

  // 390 px: no horizontal overflow.
  await page.setViewportSize({ width: 390, height: 844 })
  await page.reload() // a phone loads the page at its width: the side menu starts closed
  await card.waitFor()
  for (const n of [1, 2]) { const b = page.locator(`[data-level-step="${n}"] > button`); if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click() }
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no overflow at 390 px')
  await page.screenshot({ path: path.join(shots, 'screw-corner-quick-390.png'), fullPage: true })
  await page.setViewportSize({ width: 1440, height: 1100 })

  // History: a one-corner quick entry is named and shows only its values.
  await page.route('**/api/screws/history', route =>
    route.request().method() === 'GET'
      ? route.fulfill({ json: { sessions: [{ kind: 'quick', started: 100, ended: 160, measurements: [{ time: 160, kind: 'quick', corner: 'FR', twice: false, z: { FL: -0.7, FR: -0.55 }, bed: 60 }] }] } })
      : route.fallback()
  )
  // The page looks at the saved trail every 20 s while nothing runs: look now.
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')))
  await card.locator('#screw-history summary').first().click()
  const entry = card.locator('[data-screw-kind="quick"]')
  await entry.locator('[data-col="kind"]').getByText('Corner', { exact: true }).waitFor({ timeout: 8000 })
  assert.equal(await entry.locator('[data-screw-corner]').getAttribute('title'), 'Front right')
  await entry.locator('[data-col="FR"]').getByText('+150', { exact: true }).waitFor()
  assert.equal(await entry.locator('[data-col="RR"]').innerText(), '—', 'corners that were not measured show a dash')

  // During a session there is the session's own re-probe, not the quick one.
  await page.route('**/api/printer', async route => {
    const data = await (await route.fetch()).json()
    data.screws_session = { active: true, xy_homed: true, bed: 60 }
    return route.fulfill({ json: data })
  })
  await page.waitForFunction(() => !document.querySelector('[data-screw-quick-corner]'), null, { timeout: 8000 })
  assert.equal(await card.locator('[data-screw-quick-continue]').count(), 0)
  console.log('PASS screw corner quick measurement: merge, warning, shift notice, history, session guard, 390 px')
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
