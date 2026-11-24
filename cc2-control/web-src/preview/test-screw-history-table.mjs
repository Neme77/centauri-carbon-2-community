// The screw trail ("Earlier sessions") as a table: one row per measurement, the values in their columns, the unit once in the
// header, a date row where the day changes, a head row for a session of several measurements, two lines per measurement on a
// phone without a horizontal overflow of the page. Every history is a local fixture.
import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
const shots = path.join(root, 'preview/artifacts')
const errors = []

// Local times, so the expected clock text does not depend on the time zone of the machine running the test.
const at = (day, h, m, s) => Math.floor(new Date(2026, 10, day, h, m, s).getTime() / 1000)
const clock = unix => {
  const d = new Date(unix * 1000)
  return [d.getHours(), d.getMinutes(), d.getSeconds()].map(n => String(n).padStart(2, '0')).join(':')
}
const T = {
  quickA: at(17, 9, 5, 3),
  quickB: at(17, 8, 40, 59),
  start: at(17, 10, 0, 5),
  probe: at(17, 10, 10, 0),
  check: at(17, 10, 20, 7),
  end: at(17, 10, 21, 30),
  oldStart: at(16, 23, 58, 5),
}
const history = [
  {
    kind: 'session',
    points: 'screws',
    started: T.start - 5,
    ended: T.end,
    measurements: [
      { time: T.start, kind: 'start', corner: null, twice: false, z: { FL: 1.8, FR: 1.74, RR: 1.72, RL: 1.7 }, bed: 60, ist: { bed: 61.7, nozzle: 139.8, chamber: 23 } },
      {
        time: T.probe,
        kind: 'probe',
        corner: 'FR',
        twice: true,
        z: { FR: 1.75 },
        z2: { FR: 1.77 },
        probes: { FR: [1.75, 1.75, 1.75, 1.77, 1.77, 1.77] },
        bed: 60,
        ist: { bed: 60.2, nozzle: 140.1 },
      },
      { time: T.check, kind: 'check', corner: null, twice: false, z: { FL: 1.8, FR: 1.78, RR: 1.76, RL: 1.74 }, bed: 60, ist: { bed: 60.1, nozzle: 140, chamber: 24 } },
    ],
  },
  {
    kind: 'session',
    points: 'old',
    started: T.oldStart,
    ended: T.oldStart + 600,
    measurements: [{ time: T.oldStart + 30, kind: 'start', corner: null, twice: false, z: { FL: 1.5, FR: 1.52, RR: 1.47, RL: 1.5 }, bed: 55 }],
  },
  {
    kind: 'quick',
    points: 'screws',
    started: T.quickA - 120,
    ended: T.quickA,
    measurements: [{ time: T.quickA, kind: 'quick', corner: null, twice: false, z: { FL: -0.7, FR: -0.68, RR: -0.8, RL: -0.73 }, bed: 60, ist: { bed: 60.4, nozzle: 140, chamber: 22 } }],
  },
  {
    kind: 'quick',
    points: 'screws',
    started: T.quickB - 90,
    ended: T.quickB,
    measurements: [{ time: T.quickB, kind: 'quick', corner: 'RL', twice: false, z: { FL: -0.7, RL: -0.75 }, bed: 60 }],
  },
]

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
  await page.addInitScript(() => {
    try {
      localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' }))
    } catch {}
  })
  page.on('pageerror', e => errors.push(e.message))
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  await page.route('**/api/screws/history', route => (route.request().method() === 'GET' ? route.fulfill({ json: { sessions: history } }) : route.fallback()))
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.goto(`${origin}/#bed/screws`)
  const card = page.locator('#screwFocus')
  await card.waitFor()
  const historyBox = card.locator('#screw-history')
  await historyBox.locator('summary').first().click()
  const table = historyBox.locator('[data-screw-history-table="sessions"]')
  const quick = historyBox.locator('[data-screw-history-table="quick"]')
  await table.waitFor()

  // A real table: a name, head cells with a column scope, the unit once in the head and not in the cells.
  assert.equal(await table.locator('thead th[scope="col"]').count(), 9)
  assert.deepEqual(
    await table.locator('thead th').allInnerTexts().then(list => list.map(x => x.replace(/\s+/g, ' ').trim())),
    ['Time', 'Type', 'Bed °C', 'Nozzle °C', 'Chamber °C', 'Front left µm', 'Front right µm', 'Back right µm', 'Back left µm']
  )
  assert.equal(await table.getAttribute('aria-label'), 'Earlier sessions')
  const bodyText = await table.locator('tbody td:visible').allInnerTexts()
  assert.equal(bodyText.some(x => /µm|°C/.test(x.replace(/Bed target \d+ °C/, ''))), false, 'no visible cell repeats a unit')

  // Two sessions: the first with three measurements (a head row), the second with one (no head row). Four measurement rows.
  const rows = table.locator('tr[data-screw-row]')
  assert.equal(await rows.count(), 4, 'one row per measurement')
  assert.equal(await table.locator('[data-screw-session]').count(), 1, 'only the session of several measurements has a head row')
  assert.equal(await table.locator('[data-screw-date]').count(), 2, 'a date row where the day changes')
  const order = await table.locator('tbody > tr:not([data-screw-row-corners]):not([data-screw-detail])').evaluateAll(list =>
    list.map(tr => (tr.hasAttribute('data-screw-date') ? 'date' : tr.hasAttribute('data-screw-session') ? 'head' : tr.hasAttribute('data-screw-row') ? 'row' : 'narrow'))
  )
  assert.deepEqual(order.filter(x => x !== 'narrow'), ['date', 'head', 'row', 'row', 'row', 'date', 'row'])
  const head = table.locator('[data-screw-session]')
  assert.equal(await head.locator('[data-screw-range]').innerText(), `${clock(T.start - 5)}–${clock(T.end)}`)
  await head.locator('[data-screw-bed-target]').getByText('Bed target 60 °C', { exact: true }).waitFor()
  for (const [id, v] of [['FL', '0'], ['FR', '−20'], ['RR', '−40'], ['RL', '−60']]) {
    assert.equal(await head.locator(`[data-col="${id}"]`).innerText(), v, `head row, final value ${id}`)
  }

  const cell = (row, col) => row.locator(`[data-col="${col}"]`).innerText().then(x => x.replace(/\s+/g, ' ').trim())
  // Row 1: the start of the session, all four corners against front left, the actual temperatures.
  assert.equal(await cell(rows.nth(0), 'time'), clock(T.start))
  assert.equal(await cell(rows.nth(0), 'kind'), 'Initial')
  assert.deepEqual(
    await Promise.all(['bed', 'nozzle', 'chamber', 'FL', 'FR', 'RR', 'RL'].map(c => cell(rows.nth(0), c))),
    ['61.7', '139.8', '23', '0', '−60', '−80', '−100']
  )
  // Row 2: a re-probe of front right with 2× 3 probes: the average of both results, the other corners are dashes.
  assert.equal(await cell(rows.nth(1), 'kind'), 'Re-measure FR 2× 3', 'type, corner and the 2× 3 shorthand')
  assert.equal(await rows.nth(1).locator('[data-screw-corner]').getAttribute('title'), 'Front right')
  assert.equal(await rows.nth(1).locator('[data-screw-twice]').getAttribute('title'), '2× 3 probes')
  assert.deepEqual(
    await Promise.all(['bed', 'nozzle', 'chamber', 'FL', 'FR', 'RR', 'RL'].map(c => cell(rows.nth(1), c))),
    ['60.2', '140.1', '—', '—', '−40', '—', '—']
  )
  // Row 3: the full check.
  assert.equal(await cell(rows.nth(2), 'kind'), 'Check')
  assert.deepEqual(await Promise.all(['FL', 'FR', 'RR', 'RL'].map(c => cell(rows.nth(2), c))), ['0', '−20', '−40', '−60'])
  // Row 4: the session of one measurement, probed at the earlier points: the note is a hint at the type.
  assert.equal(await cell(rows.nth(3), 'time'), clock(T.oldStart + 30))
  assert.match(await rows.nth(3).locator('[data-screw-old-points] button').getAttribute('aria-label'), /Measured at the earlier points/)
  assert.deepEqual(await Promise.all(['bed', 'FL', 'FR', 'RR', 'RL'].map(c => cell(rows.nth(3), c))), ['—', '0', '+20', '−30', '0'])
  // Numbers sit right-aligned in tabular figures.
  const style = await rows.nth(0).locator('[data-col="FR"]').evaluate(el => ({ align: getComputedStyle(el).textAlign, nums: getComputedStyle(el).fontVariantNumeric }))
  assert.equal(style.align, 'right')
  assert.match(style.nums, /tabular-nums/)
  // The detail row: the bed target and the single probes of the measurement that are not in the row.
  assert.equal(await table.locator('[data-screw-detail]').count(), 0, 'the details are folded')
  await rows.nth(1).locator('[data-screw-toggle]').click()
  const detail = table.locator('[data-screw-detail]')
  await detail.getByText('Bed target 60 °C', { exact: true }).waitFor()
  assert.equal(await detail.locator('[data-detail-corner="FR"]').innerText(), 'Front right: 1.750 · 1.750 · 1.750 · 1.770 · 1.770 · 1.770')
  assert.equal(await rows.nth(1).locator('[data-screw-toggle]').getAttribute('aria-expanded'), 'true')
  await rows.nth(1).locator('[data-screw-toggle]').click()
  assert.equal(await table.locator('[data-screw-detail]').count(), 0)

  // The quick measurements: a full one and a one-corner one with its corner; the hint replaces "no 30 s pause".
  const qrows = quick.locator('tr[data-screw-row]')
  assert.equal(await qrows.count(), 2)
  assert.equal(await quick.locator('[data-screw-date]').count(), 1, 'both on one day: one date row')
  assert.equal(await cell(qrows.nth(0), 'kind'), 'Quick measurement')
  assert.equal(await qrows.nth(0).locator('[data-hint="no-pause"] button').getAttribute('aria-label'), 'Measured without the 30 s pause.')
  assert.deepEqual(
    await Promise.all(['bed', 'nozzle', 'chamber', 'FL', 'FR', 'RR', 'RL'].map(c => cell(qrows.nth(0), c))),
    ['60.4', '140.0', '22', '0', '+20', '−100', '−30']
  )
  assert.equal(await cell(qrows.nth(1), 'kind'), 'Corner BL')
  assert.equal(await qrows.nth(1).locator('[data-screw-corner]').getAttribute('title'), 'Back left')
  assert.deepEqual(await Promise.all(['FL', 'FR', 'RR', 'RL'].map(c => cell(qrows.nth(1), c))), ['0', '—', '—', '−50'])
  // Exports and clearing stay.
  for (const name of ['Export JSON', 'Export CSV', 'CSV for Excel (DE)', 'Clear history'])
    await historyBox.getByRole('button', { name, exact: true }).waitFor()
  await page.addStyleTag({ content: '#cc2-preview-banner{display:none!important}' })
  await historyBox.screenshot({ path: path.join(shots, 'screw-history-table-1440.png') })

  // In millimetres the head states mm and the cells follow with three decimals.
  await page.evaluate(() => localStorage.setItem('cc2-screw-height-unit', 'mm'))
  await page.reload()
  await card.waitFor()
  await historyBox.locator('summary').first().click()
  await table.waitFor()
  assert.match(await table.locator('thead th').nth(6).innerText(), /mm/)
  assert.equal(await cell(table.locator('tr[data-screw-row]').nth(0), 'FR'), '−0.060')
  await page.evaluate(() => localStorage.removeItem('cc2-screw-height-unit'))

  // A phone: two lines per measurement, no horizontal overflow of the page, the table inside the screen.
  await page.setViewportSize({ width: 390, height: 844 })
  await page.reload()
  await card.waitFor()
  for (const n of [1, 2]) {
    const b = page.locator(`[data-level-step="${n}"] > button`)
    if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
  }
  await historyBox.locator('summary').first().click()
  await table.waitFor()
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no horizontal overflow at 390 px')
  const tableBox = await table.boundingBox()
  assert.ok(tableBox.x >= 0 && tableBox.x + tableBox.width <= 390 + 1, `the table lies inside the screen: ${JSON.stringify(tableBox)}`)
  assert.equal(await table.locator('thead th:visible').count(), 5, 'the corner columns move to the second line')
  const narrow = table.locator('tr[data-screw-row-corners]')
  assert.equal(await narrow.count(), 4)
  for (let i = 0; i < 4; i++) assert.equal(await narrow.nth(i).isVisible(), true)
  assert.equal(await table.locator('tr[data-screw-row] [data-col="FR"]').first().isVisible(), false)
  assert.match(await narrow.nth(0).locator('[data-narrow-corner="FR"]').innerText(), /^FR\s*−60$/)
  assert.match(await narrow.nth(1).locator('[data-narrow-corner="RR"]').innerText(), /^BR\s*—$/)
  await page.addStyleTag({ content: '#cc2-preview-banner{display:none!important}header{position:static!important}' }) // the simulation bar would cover the picture
  await historyBox.screenshot({ path: path.join(shots, 'screw-history-table-390.png') })
  const quickBox = await quick.boundingBox()
  assert.ok(quickBox.x + quickBox.width <= 391, 'the quick table lies inside the screen')

  console.log('PASS screw history table: rows, columns, dates, session head, details, quick table, 390 px')
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
