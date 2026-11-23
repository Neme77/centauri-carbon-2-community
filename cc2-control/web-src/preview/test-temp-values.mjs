import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { formatLiveValue, formatProfileValue, formatTargetValue, liveStale, LIVE_STALE_MS } from '../src/lib/temp-values.ts'

// Temperature values of the Bed alignment tab: the number formats; the bed temperature from step 1 (a material,
// "Own temperature", limits 30 to 110 °C) in the row "Bed / Target" of step 2 and in the action that goes out; the live
// values during a session. The old "Profile" row and the "~" mark of a rounded profile value went with the rebuild.
assert.equal(formatProfileValue(62.5, 'de'), '62,5', 'the profile value is written as the language writes numbers')
assert.equal(formatProfileValue(62.5, 'en'), '62.5')
assert.equal(formatProfileValue(60, 'de'), '60', 'a whole profile value has no decimals')
assert.equal(formatTargetValue(63, true), '~63', 'a rounded value measured at carries "~"')
assert.equal(formatTargetValue(60, false), '60', 'an exact one does not')
assert.equal(formatLiveValue(61.75, 'de', 1), '61,8', 'a live value has one decimal in the language')
assert.equal(formatLiveValue(28, 'en', 1), '28.0')
assert.equal(formatLiveValue(29, 'en', 0), '29', 'the chamber sensor reads whole degrees')
assert.equal(liveStale(1000, true, 1000 + LIVE_STALE_MS), false, 'a status exactly 10 s old is still current')
assert.equal(liveStale(1000, true, 1001 + LIVE_STALE_MS), true, 'an older one is not')
assert.equal(liveStale(1000, false, 1001), true, 'a failed status request is not current')
assert.equal(liveStale(0, true, 1), true, 'nothing received yet is not current')

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
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const open = async (lang, tail = '') => {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
    page.on('pageerror', e => errors.push(e.message))
    await page.addInitScript(l => localStorage.setItem('cc2-language', l), lang)
    await page.route('**/api/preferences', route => route.fulfill({ json: { language: lang } }))
    await page.route('**/i18n/*.json', route => {
      const code = new URL(route.request().url()).pathname.match(/\/i18n\/(\w+)\.json/)[1]
      route.fulfill({ contentType: 'application/json', body: readFileSync(path.join(root, 'public/locales', `${code}.json`), 'utf8') })
    })
    await page.route('**/api/material-presets', route =>
      route.fulfill({
        json: [
          { name: 'PLA', nozzle: 200, bed: 60, min: 190, max: 230 },
          { name: 'PLA-62.5', nozzle: 205, bed: 62.5, min: 190, max: 230 },
          { name: 'ABS-hot', nozzle: 260, bed: 130, min: 240, max: 280 },
        ],
      })
    )
    const sent = []
    await page.route('**/api/control', route => {
      const body = route.request().postData() || ''
      if (route.request().method() !== 'POST' || !body.startsWith('screws:measure')) return route.fallback()
      sent.push(body)
      return route.fulfill({ json: { accepted: true } })
    })
    await page.goto(origin)
    await page.locator('#cc2-preview-banner').waitFor()
    await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
    await page.locator('#cc2-preview-banner').waitFor()
    await page.goto(`${origin}/#bed/align${tail}`)
    const card = page.locator('#screwFocus')
    await card.waitFor()
    for (const n of [2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
    // The hold time of step 1 goes in front of every start the user makes; 0 here (test-bed-mesh-tick covers it).
    await card.locator('[data-level-soak]').selectOption('0')
    return { page, card, values: card.locator('#screw-temp-values'), sent }
  }
  const text = async loc => (await loc.innerText()).replace(/\s+/g, ' ').trim()

  // English. Step 1 gives the bed temperature; step 2 shows it as the target and the action carries it.
  const { page, card, values, sent } = await open('en')
  const basis = card.locator('[data-level-base]')
  const bed = values.locator('[data-temp-row="bed"]')
  const start = card.getByRole('button', { name: /Start session/ })
  const quick = card.getByRole('button', { name: /Measure only \(quick\)/ })
  const invalid = card.locator('[data-level-temp-invalid]')
  const mode = name => card.locator(`[data-level-start-mode-button="${name}"]`).click()
  // Both starts are blocked: look at each mode in turn (only the button of the chosen mode is on the page).
  const bothBlocked = async () => {
    await mode('session')
    const session = await start.isDisabled()
    await mode('quick')
    return session && (await quick.isDisabled())
  }
  const bothReleased = async () => {
    await mode('session')
    const session = await start.isEnabled()
    await mode('quick')
    return session && (await quick.isEnabled())
  }
  // Before a session step 1 shows one row, the bed (live, then the target); no nozzle, no chamber.
  assert.equal(await values.locator('[data-temp-row]').count(), 1, 'one bed row before a session')
  assert.equal(await values.locator('[data-temp-row="nozzle"]').count(), 0, 'no nozzle row before a session')
  await basis.selectOption('material:PLA-62.5')
  assert.match(await basis.evaluate(e => e.options[e.selectedIndex].text), /PLA-62\.5 · 63 °C/, 'the choice names the rounded temperature')
  assert.equal(await bed.locator('[data-value="profile"]').count(), 0, 'no "Profile" row any more (gone with the rebuild)')
  assert.equal(await card.getByText('Target ~', { exact: false }).count(), 0, 'no "~" mark any more: the value is rounded in step 1')
  await quick.click()
  await page.getByRole('dialog').getByText('Heat the bed to 63 °C').waitFor()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => true)
  await new Promise(r => setTimeout(r, 400))
  assert.deepEqual(sent, ['screws:measure:63:2'], '62.5 °C goes out as 63, with 2× 3 probes')
  await basis.selectOption('material:PLA')
  await quick.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await new Promise(r => setTimeout(r, 400))
  assert.equal(sent.at(-1), 'screws:measure:60:2', 'at 60 °C with 2× 3 probes the bed is named: the trailing :2 needs it')
  // A material above the bed's 110 °C is not measured: the message sits in step 1, both starts are blocked.
  await basis.selectOption('material:ABS-hot')
  await invalid.waitFor()
  assert.equal(await card.locator('[data-level-step="1"] [data-level-temp-invalid]').count(), 1)
  assert.ok(await bothBlocked(), 'above the limit both starts are blocked')
  // "Own temperature": 30 to 110 whole degrees.
  await basis.selectOption('custom')
  const own = card.locator('[data-level-custom-temp]')
  for (const [value, ok] of [['29', false], ['30', true], ['110', true], ['111', false], ['60.5', false], ['', false], ['100', true]]) {
    await own.fill(value)
    if (ok) {
      await invalid.waitFor({ state: 'detached' })
      assert.ok(await bothReleased(), `${value} °C: both starts are released`)
    } else {
      await invalid.waitFor()
      assert.ok(await bothBlocked(), `"${value}" blocks both starts`)
    }
  }
  const before = sent.length
  await quick.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await new Promise(r => setTimeout(r, 400))
  assert.equal(sent.length, before + 1)
  assert.equal(sent.at(-1), 'screws:measure:100:2', 'any other temperature goes out as a suffix')

  // During a session: the value the session measures at, from the printer service, and the live values.
  await basis.selectOption('material:PLA-62.5')
  await mode('session')
  await start.click()
  await page.getByRole('dialog').getByText('heat the bed to 63 °C').waitFor()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await bed.locator('[data-value="live"]').getByText('61.8').waitFor() // the idle row already has a live value: wait for the session's
  assert.equal(await text(bed.locator('[data-value="target"]')), 'Target 63 °C', 'the session reports whole degrees')
  assert.equal(await bed.locator('[data-value="profile"]').count(), 0, 'the session does not know the profile')
  assert.equal(await values.locator('[data-value="measured"]').count(), 0, 'no measurement yet: no "Measured at" value in any row')
  assert.equal(await text(bed.locator('[data-value="live"]')), '61.8 °C Live')
  assert.equal(await text(values.locator('[data-temp-row="nozzle"]')), 'Nozzle 28.0 °C Live Target –')
  assert.equal(await text(values.locator('[data-temp-row="chamber"]')), 'Chamber 29 °C Live Target –')
  // Fixed columns: the live values, the targets and the measured values of the three rows stand exactly one under the other,
  // and the numbers end on the same line (right-aligned, tabular figures).
  const edge = async (row, value, side) =>
    values.locator(`[data-temp-row="${row}"] [data-value="${value}"]`).evaluate((e, s) => Math.round(e.getBoundingClientRect()[s]), side)
  for (const [value, side] of [['live', 'left'], ['target', 'left'], ['target-none', 'left'], ['live', 'right']]) {
    const a = await edge('bed', value === 'target-none' ? 'target' : value, side)
    const b = await edge('nozzle', value === 'target' ? 'target-none' : value, side)
    const c = await edge('chamber', value === 'target' ? 'target-none' : value, side)
    assert.ok(a === b && b === c, `the ${value} column starts/ends at the same place in all rows (${a}/${b}/${c})`)
  }
  assert.equal(await values.locator('[data-temp-row="bed"] [data-value="target"]').evaluate(e => getComputedStyle(e.lastElementChild).fontVariantNumeric), 'tabular-nums', 'tabular figures')
  assert.equal(await values.locator('[data-value="target-none"]').count(), 2, 'a dash as the target of nozzle and chamber')
  assert.equal(await bed.locator('[data-value="live"]').getAttribute('data-stale'), 'false')
  await page.screenshot({ path: path.join(shots, 'temp-values-session-1440.png'), fullPage: false })

  // No printer status any more: the live values are marked as not current, in text and colour.
  await page.route('**/api/printer', route => route.abort())
  await bed.locator('[data-value="live"][data-stale="true"]').waitFor({ timeout: 20000 })
  assert.equal(await text(bed.locator('[data-value="live"]')), '61.8 °C not current')
  assert.match(await bed.locator('[data-value="live"]').getAttribute('class'), /text-muted/)
  await page.unroute('**/api/printer')
  await bed.locator('[data-value="live"][data-stale="false"]').waitFor({ timeout: 20000 })
  // A status request that hangs: request() aborts it after 8 s, so the values turn "not current" as well.
  await page.route('**/api/printer', () => {})
  await bed.locator('[data-value="live"][data-stale="true"]').waitFor({ timeout: 25000 })
  await page.unroute('**/api/printer')
  await page.reload()
  await page.locator('#cc2-preview-banner').waitFor()
  await bed.locator('[data-value="live"][data-stale="false"]').waitFor({ timeout: 20000 }).catch(async () => {
    for (const n of [2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
    await bed.locator('[data-value="live"][data-stale="false"]').waitFor({ timeout: 20000 })
  })
  await page.locator('[data-level-session-bar]').getByRole('button', { name: /End session/ }).click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click().catch(() => {})
  await page.close()

  // German: numbers and labels of the language.
  const de = await open('de')
  await de.card.locator('[data-level-base]').selectOption('material:PLA-62.5')
  assert.match(await de.card.locator('[data-level-base]').evaluate(e => e.options[e.selectedIndex].text), /PLA-62\.5 · 63 °C/)
  await de.card.locator('[data-level-base]').selectOption('custom')
  await de.card.locator('[data-level-custom-temp]').fill('111')
  await de.card.locator('[data-level-temp-invalid]').getByText('ganze Betttemperatur von 30 bis 110 °C').waitFor()
  await de.page.close()

  assert.deepEqual(errors, [], 'no page errors')
  console.log('PASS temperature values: bed temperature from step 1 (material, own temperature, limits) as target and action, live values during a session, live marked when not current')
} finally {
  await browser?.close()
  await server.close()
}
