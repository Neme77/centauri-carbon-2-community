// The strategy of the screw plan on the Bed alignment tab (review of #403): front left stays the fixed measurement reference
// and the default adjustment reference. The optimized plan (front left is turned too) is only offered, in its own note with
// a button, and used after the operator's explicit choice. The choice is kept in the browser tab, nothing switches it between
// measurements, and the way back is a button. What was measured (against front left) stands apart from the suggestion. After
// the front-left screw moved, no new plan is shown before all four corners are measured again. Probe values are synthetic.
import assert from 'node:assert/strict'
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
const shots = process.env.CC2_SCREENSHOTS
const THEMES = ['light', 'dark', 'dracula', 'nord', 'monokai', 'solarized-light', 'obsidian']
const errors = []
const actions = []
let browser
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH
      ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] }
      : process.env.CC2_BROWSER_EXECUTABLE
        ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE }
        : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const heightsOf = z => Object.fromEntries(['27.9,25.95', '227.15,25.95', '227.15,225.5', '27.9,225.5'].map((k, i) => [k, z[i]]))
  const tramOutput = heights =>
    `SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n${Object.entries(heights)
      .map(([key, z]) => {
        const [x, y] = key.split(',')
        const h = Number(z).toFixed(6)
        return `${`// probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${h}\n`.repeat(3)}// Result is z=${h}\n`
      })
      .join('')}`
  const start = (z, time = 10) => ({ time, kind: 'start', corner: null, twice: false, z: { FL: z[0], FR: z[1], RR: z[2], RL: z[3] } })
  const fulfill = body => ({ contentType: 'application/json', body: JSON.stringify(body) })

  // A page with a running screw session whose measurements the test sets (`show`).
  const open = async ({ viewport = { width: 1440, height: 1000 }, storage = true, empty = false, session = true } = {}) => {
    const page = await browser.newPage({ viewport })
    page.on('pageerror', e => errors.push(e.message))
    page.on('request', r => {
      if (r.method() === 'POST' && r.url().endsWith('/api/control')) actions.push(r.postData())
    })
    await page.addInitScript(() => {
      try {
        localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' }))
        localStorage.setItem('cc2-screw-turn-mode', 'exact')
      } catch {}
    })
    if (!storage)
      await page.addInitScript(() => {
        // A browser without a usable sessionStorage (private window, blocked site data): every access throws.
        Object.defineProperty(window, 'sessionStorage', {
          get() {
            throw new Error('blocked')
          },
        })
      })
    const state = { preview: null }
    await page.route('**/api/console', route => (state.preview?.console && route.request().method() === 'GET' ? route.fulfill(fulfill(state.preview.console)) : route.fallback()))
    await page.route('**/api/printer', async route => {
      const data = await (await route.fetch()).json()
      data.screws_session = { active: session, xy_homed: session }
      return route.fulfill({ json: data })
    })
    await page.route('**/api/screws/history', route => (state.preview?.history && route.request().method() === 'GET' ? route.fulfill(fulfill({ sessions: state.preview.history })) : route.fallback()))
    let generation = 100
    state.show = (z, measurements = [start(z)]) => {
      generation += 1
      state.preview = {
        console: { busy: false, completed: true, success: true, generation, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n', output: tramOutput(heightsOf(z)) },
        history: [{ started: 10, ended: null, measurements }],
      }
    }
    if (empty) state.preview = { console: { busy: false, completed: true, success: true, generation: 99, command: '', output: '' }, history: [{ started: 10, ended: null, measurements: [] }] }
    else state.show([1.8, 1.74, 1.72, 1.7])
    await page.goto(origin)
    await page.locator('#cc2-preview-banner').waitFor()
    await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
    await page.locator('#cc2-preview-banner').waitFor()
    await page.goto(`${origin}/#bed/screws`)
    const card = page.locator('#screwFocus')
    await card.waitFor()
    for (const n of [1, 2]) {
      const b = page.locator(`[data-level-step="${n}"] > button`)
      if ((await b.count()) && (await b.getAttribute('aria-expanded')) !== 'true') await b.click()
    }
    return { page, card, state }
  }
  const text = async loc => (await loc.innerText()).replace(/ /g, ' ').trim()
  const stored = page => page.evaluate(() => sessionStorage.getItem('cc2-screw-strategy'))
  const label = card => card.locator('[data-screw-strategy]')

  {
    const { page, card, state } = await open()
    const strategy = label(card)
    const offer = card.locator('[data-screw-optimize-suggest]')
    const mrow = id => card.locator(`[data-screw-measured-row="${id}"]`)
    const mainButton = card.locator('[data-screw-check]')
    const singles = () => card.locator('[data-screw-quick-corner], button[aria-label^="Single measurement"]')

    // 1. Standard is the default, clearly named, with front left as the reference. The optimized plan is only offered.
    await strategy.waitFor({ timeout: 10000 })
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'standard')
    assert.equal(await text(strategy), 'Strategy: Standard (front-left reference)')
    await offer.waitFor()
    assert.equal(await text(offer.locator('p').first()), 'Optimized correction available')
    await offer.getByText('Also lower the front-left screw by 60 µm').waitFor()
    await offer.getByText('also turns the reference screw (front left)', { exact: false }).waitFor()
    assert.equal(await text(offer.getByRole('button').first()), 'Choose optimized correction')
    assert.equal(await text(offer.getByRole('button').nth(1)), 'Stay with standard')
    assert.equal(await text(card.locator('[data-screw-adjust="FL"]')), 'No turn', 'standard: front left is left as it is')
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Leave unchanged')
    assert.equal(await card.locator('[data-screw-combined-note]').count(), 0)
    assert.equal(await card.locator('[data-screw-strategy-back]').count(), 0, 'no way back while on the standard')
    assert.equal(await text(mainButton), 'Measure again', 'the usual button while front left stays')
    for (const b of await singles().all()) assert.ok(await b.isEnabled(), 'single corners can be measured with the standard plan')
    assert.equal(await stored(page), null, 'looking at the offer stores nothing')

    // 2. What was measured stands apart, always against front left: a table of four rows, then the suggestion table.
    assert.equal(await text(card.locator('[data-screw-measured-block] h4')), 'Measured')
    assert.equal(await text(card.locator('[data-screw-measured-block] [data-screw-block-note]')), 'Always relative to front left')
    assert.equal(await text(card.locator('[data-screw-proposal-block] h4')), 'Suggestion for adjusting')
    assert.equal(await text(card.locator('[data-screw-proposal-block] [data-screw-block-note]')), 'Calculated plan for the chosen strategy')
    const heads = table => table.locator('thead th').evaluateAll(es => es.map(e => e.textContent.trim()))
    assert.deepEqual(await heads(card.locator('[data-screw-measured-table]')), ['Position', 'Z height (absolute)', 'Deviation from front left', 'Status'], 'the measured block is a table with four columns')
    assert.deepEqual(await heads(card.locator('[data-screw-proposal-block] table')), ['Position', 'Target', 'Direction & amount', 'Turn aid'])
    assert.equal(await card.locator('[data-screw-measured-table] tbody > tr[data-screw-measured-row]').count(), 4)
    assert.equal(await card.locator('[data-screw-tile], [data-screw-tiles]').count(), 0, 'no tiles any more')
    const offsets = async () => Promise.all(['FL', 'FR', 'RR', 'RL'].map(id => text(card.locator(`[data-screw-offset="${id}"]`))))
    assert.deepEqual(await offsets(), ['0 µm', '−60 µm', '−80 µm', '−100 µm'])
    await mrow('FL').locator('[data-screw-measured="FL"]').getByText('Reference', { exact: true }).waitFor()
    assert.equal(await text(card.locator('[data-screw-zheight="FR"]')), '1740 µm')
    const block = async sel => (await card.locator(sel).boundingBox())
    {
      const m = await block('[data-screw-measured-block]')
      const p = await block('[data-screw-proposal-block]')
      assert.ok(m.y + m.height <= p.y + 1, 'the suggestion stands under what was measured')
      const o = await offer.boundingBox()
      const t = await block('[data-screw-measured-table]')
      assert.ok(o.y + o.height <= t.y + 1, 'the offer stands above the measured table')
      const ys = await card.locator('[data-screw-measured-row]').evaluateAll(es => es.map(e => Math.round(e.getBoundingClientRect().y)))
      assert.deepEqual([...ys].sort((a, b) => a - b), ys, 'rows in order')
      assert.equal(new Set(ys).size, 4, 'four rows one below the other')
      const tag = await card.locator('[data-screw-strategy]').boundingBox()
      const head = await card.locator('[data-screw-proposal-block] h4').boundingBox()
      const blockBox = await block('[data-screw-proposal-block]')
      const note = await block('[data-screw-proposal-block] [data-screw-block-note]')
      assert.ok(tag.x >= note.x + note.width - 1 && Math.abs(tag.y + tag.height / 2 - (note.y + note.height / 2)) < 12, 'the strategy tag stands right beside the note of the head of the suggestion block')
      assert.ok(note.y + note.height > head.y, 'the note and the tag belong to the head of the block')
      assert.ok(tag.y >= blockBox.y && tag.y + tag.height <= blockBox.y + blockBox.height, 'inside the suggestion block')
      assert.equal(await page.locator('[data-level-step="2"] > button [data-screw-strategy]').count(), 0, 'not in the head of step 2')
    }

    // 3. No automatic switch between measurements: other heights, still standard, the offer follows what the plan says.
    state.show([1.8, 1.85, 1.77, 1.81])
    await card.locator('[data-screw-zheight="FR"]').getByText('1850 µm').waitFor({ timeout: 8000 })
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'standard')
    assert.equal(await offer.count(), 0, 'nothing to offer for these heights')
    assert.deepEqual(await offsets(), ['0 µm', '+50 µm', '−30 µm', '+10 µm'])
    state.show([1.8, 1.74, 1.72, 1.7], [start([1.8, 1.74, 1.72, 1.7], 12)])
    await offer.waitFor({ timeout: 8000 })
    assert.equal(await text(card.locator('[data-screw-adjust="FL"]')), 'No turn', 'still standard')
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Leave unchanged')

    // 4. "Stay with standard": the offer goes away and stays away over the next measurement.
    await offer.locator('[data-screw-optimize-keep]').click()
    assert.equal(await offer.count(), 0)
    state.show([1.8, 1.73, 1.71, 1.69], [start([1.8, 1.73, 1.71, 1.69], 14)])
    await card.locator('[data-screw-zheight="FR"]').getByText('1730 µm').waitFor({ timeout: 8000 })
    assert.equal(await offer.count(), 0, 'a declined offer is not repeated')
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'standard')
    // The way back stays: a small button of its own, only while the optimized plan brings something, and it does the same
    // as the offer; "Back to standard" then declines again.
    const again = card.locator('[data-screw-optimize-again]')
    assert.equal(await again.count(), 1, 'after "Stay with standard" the optimized plan can still be chosen')
    assert.equal(await text(again), 'Choose optimized correction')
    await again.click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    assert.equal(await again.count(), 0, 'the button is gone while the optimized plan is chosen')
    await card.locator('[data-screw-strategy-back]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'standard')
    assert.equal(await offer.count(), 0, 'the offer itself stays declined')
    assert.equal(await again.count(), 1, 'and the small button is back')
    state.show([1.8, 1.74, 1.72, 1.7], [start([1.8, 1.74, 1.72, 1.7], 16)])
    await card.locator('[data-screw-zheight="FR"]').getByText('1740 µm').waitFor({ timeout: 8000 })

    // 5. The explicit choice: clearly named, kept in the tab, front left is adjusted, what was measured does not move.
    await page.evaluate(() => sessionStorage.removeItem('cc2-screw-strategy-kept'))
    await page.reload()
    await card.waitFor()
    await offer.waitFor({ timeout: 10000 })
    await offer.locator('[data-screw-optimize-choose]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    assert.equal(await text(strategy), 'Strategy: Optimized (includes the front-left screw)')
    assert.equal(await stored(page), 'optimized', 'the choice is kept in the browser tab')
    assert.equal(await offer.count(), 0)
    await card.locator('[data-screw-combined-note]').waitFor()
    assert.equal(await text(card.locator('[data-screw-amount="FL"]')), '60 µm')
    assert.equal(await card.locator('[data-screw-direction="lower"]').count() >= 1, true)
    assert.deepEqual(await offsets(), ['0 µm', '−60 µm', '−80 µm', '−100 µm'], 'measured offsets do not change with the strategy')
    // The suggestion carries the plan, not the measurement: front right measured −60 µm, but after front left has moved by 60 µm
    // the plan has nothing left to do there. Measured says "lies lower", the status follows the plan: in order.
    assert.equal(await card.locator('[data-screw-row="FR"] [data-screw-ok]').count(), 1, 'front right is in order in the optimized plan')
    assert.equal(await card.locator('[data-screw-amount="FR"]').count(), 0, 'no amount to turn at front right')
    assert.equal(await text(card.locator('[data-screw-no-turn-needed]').first()), 'No turn needed')
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Move toward the other three corners')
    assert.equal(await text(card.locator('[data-screw-goal="FR"]')), 'Bring to the height of front left (after its move)')
    // After the choice the next measurement is a four-corner one; single corners wait.
    assert.equal(await text(mainButton), 'Start four-corner measurement')
    for (const b of await singles().all()) {
      assert.ok(await b.isDisabled(), 'single corners wait until all four are measured again')
      assert.equal(await b.getAttribute('title'), 'Single corners can be measured only after all four corners are measured again.')
    }
    const sent = actions.length
    const posted = page.waitForRequest(r => r.method() === 'POST' && r.url().endsWith('/api/control') && r.postData() === 'screws:tram:check:2')
    await mainButton.click()
    await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
    await posted
    assert.equal(actions.length, sent + 1)
    assert.equal(actions.at(-1), 'screws:tram:check:2', 'the button runs the full four-corner measurement')

    // 6. The choice is stable: a new measurement, a mix that the optimized plan cannot use, a reload. Nothing switches back.
    state.show([1.8, 1.85, 1.77, 1.81], [start([1.8, 1.74, 1.72, 1.7]), { time: 20, kind: 'check', corner: null, twice: false, z: { FL: 1.8, FR: 1.85, RR: 1.77, RL: 1.81 } }])
    await card.locator('[data-screw-zheight="FR"]').getByText('1850 µm').waitFor({ timeout: 8000 })
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'optimized', 'a measurement does not change the strategy')
    assert.equal(await card.locator('[data-screw-strategy-back]').count(), 1)
    assert.equal(await text(card.locator('[data-screw-adjust="FL"]')), 'No turn', 'nothing to turn at front left in this measurement')
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Leave unchanged')
    assert.equal(await text(mainButton), 'Measure again', 'front left is not turned now: the usual button')
    await page.reload()
    await card.waitFor()
    await strategy.waitFor({ timeout: 10000 })
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'optimized', 'a reload keeps the choice')

    // 7. Front left moved: without a full measurement there is no new plan, no stale numbers, only the notice.
    state.show([1.812, 1.74, 1.72, 1.7], [start([1.8, 1.74, 1.72, 1.7]), { time: 21, kind: 'probe', corner: 'FL', twice: false, z: { FL: 1.812 } }])
    const needed = card.locator('[data-screw-four-needed]')
    await needed.waitFor({ timeout: 8000 })
    assert.equal(await text(needed.locator('p').first()), 'Four-corner measurement needed')
    await needed.getByText('Front left has moved by +12 µm', { exact: false }).waitFor()
    assert.equal(await card.locator('[data-screw-proposal-block]').count(), 0, 'no adjustment plan before the four-corner measurement')
    assert.equal(await card.locator('[data-screw-optimize-suggest]').count(), 0)
    assert.equal(await text(mainButton), 'Start four-corner measurement')
    for (const b of await singles().all()) assert.ok(await b.isDisabled(), 'no single corner before the four-corner measurement')
    assert.equal(await card.locator('[data-screw-measured-block]').evaluate(e => getComputedStyle(e).opacity), '0.6', 'the old measurement is dimmed')
    // A full measurement clears it, and a complete new plan is shown again.
    state.show([1.812, 1.74, 1.72, 1.7], [start([1.8, 1.74, 1.72, 1.7]), { time: 21, kind: 'probe', corner: 'FL', twice: false, z: { FL: 1.812 } }, { time: 30, kind: 'check', corner: null, twice: false, z: { FL: 1.812, FR: 1.74, RR: 1.72, RL: 1.7 } }])
    await page.waitForFunction(() => document.querySelector('[data-screw-four-needed]') === null, null, { timeout: 8000 })
    await card.locator('[data-screw-proposal-block]').waitFor()

    // 8. Back to standard is explicit, kept, and does not bring the offer back.
    await card.locator('[data-screw-strategy-back]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'standard')
    assert.equal(await text(strategy), 'Strategy: Standard (front-left reference)')
    assert.equal(await stored(page), 'standard')
    assert.equal(await card.locator('[data-screw-strategy-back]').count(), 0)
    assert.equal(await offer.count(), 0, 'coming back is not answered by the same offer')
    assert.equal(await text(card.locator('[data-screw-adjust="FL"]')), 'No turn')
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Leave unchanged')
    await page.reload()
    await card.waitFor()
    await strategy.waitFor({ timeout: 10000 })
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'standard', 'a reload keeps the way back')
    // A front-left move with the standard plan is the old note: the other values no longer match.
    state.show([1.812, 1.74, 1.72, 1.7], [start([1.8, 1.74, 1.72, 1.7]), { time: 40, kind: 'probe', corner: 'FL', twice: false, z: { FL: 1.812 } }])
    await card.getByText('no longer match', { exact: false }).waitFor()
    assert.equal(await needed.count(), 0, 'the standard plan needs no four-corner measurement')
    await page.close()
  }

  // 9. A tab without a usable sessionStorage: the page works, the choice holds in the page, standard after a reload.
  {
    const { page, card } = await open({ storage: false })
    const strategy = label(card)
    await strategy.waitFor({ timeout: 10000 })
    assert.equal(await strategy.getAttribute('data-screw-strategy'), 'standard')
    await card.locator('[data-screw-optimize-choose]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    await card.locator('[data-screw-strategy-back]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'standard')
    await page.close()
  }

  // 9b. Nothing is marked that the operator did not click: before the first measurement no row of the "Measured" block is
  // lit (no cyan frame, no tint, no mark); after a measurement only the reference row carries a quiet tint and the badge;
  // a click picks exactly its row, a second click lifts it again (like before the rebuild).
  {
    const { page, card } = await open({ empty: true })
    const block = card.locator('[data-screw-measured-block]')
    await block.waitFor({ timeout: 10000 })
    const lit = await block.evaluate(b => [...b.querySelectorAll('*')].filter(e => /(^|\s)(border-cyan|bg-cyan|outline-cyan|bg-cyan\/\d+|border-cyan\/\d+)(\s|$)/.test(e.getAttribute('class') ?? '')).map(e => e.tagName + ':' + e.getAttribute('class')))
    assert.deepEqual(lit, [], 'nothing in the measured block is highlighted before the first measurement')
    assert.equal(await card.locator('[data-screw-measured-row]').count(), 4, 'four rows before the first measurement')
    assert.deepEqual(await block.locator('[data-screw-offset]').evaluateAll(es => es.map(e => e.textContent.trim())), ['—', '—', '—', '—'], 'dashes before the first measurement')
    const looks = await block.locator('[data-screw-measured-row]').evaluateAll(es => es.map(e => { const c = getComputedStyle(e); return [c.backgroundColor, c.outlineStyle, c.borderTopColor].join('|') }))
    assert.equal(new Set(looks).size, 1, 'all four rows look the same: ' + looks.join(' / '))
    assert.equal(await card.locator('[data-picked]').count(), 0, 'no row is picked')
    assert.equal(await block.locator('[data-screw-measured]').count(), 0, 'no reference badge before a measurement')
    await page.close()
  }
  {
    const { page, card, state } = await open()
    const rows = () => card.locator('[data-screw-measured-row][data-picked]').evaluateAll(es => es.map(e => e.getAttribute('data-screw-measured-row')))
    const proposal = () => card.locator('[data-screw-row][data-picked]').evaluateAll(es => es.map(e => e.getAttribute('data-screw-row')))
    await card.locator('[data-screw-measured-row="FL"] [data-screw-measured="FL"]').waitFor()
    assert.deepEqual(await rows(), [], 'nothing picked without a click')
    const tint = await card.locator('[data-screw-measured-row]').evaluateAll(es => es.map(e => getComputedStyle(e).backgroundColor))
    assert.notEqual(tint[0], tint[1], 'the reference row has a quiet tint after the measurement')
    assert.equal(await card.locator('[data-screw-measured-row="FL"]').evaluate(e => getComputedStyle(e).outlineStyle), 'none', 'a tint, no frame')
    await card.locator('[data-screw-measured-row="RR"] td').nth(2).click()
    assert.deepEqual(await rows(), ['RR'], 'a click picks exactly this row')
    assert.deepEqual(await proposal(), ['RR'], 'the same corner in the suggestion')
    await card.locator('[data-screw-measured-row="RR"] td').nth(2).click()
    assert.deepEqual(await rows(), [], 'a second click lifts it')
    assert.deepEqual(await proposal(), [])
    await card.locator('[data-screw-measured-row="FR"] td').nth(1).click()
    await card.locator('[data-screw-row="RL"] td').nth(1).click()
    assert.deepEqual(await rows(), ['RL'], 'one corner at a time')
    void state
    await page.close()
  }

  // 10. Every theme, desktop and phone: nothing runs over the edge, the tiles stand one below the other on the phone, the
  // offer wears the warning colour of the theme.
  for (const [width, label2] of [[1440, 'desktop'], [390, 'phone']]) {
    const { page, card, state } = await open({ viewport: { width, height: 1100 } })
    const strategy = label(card)
    await strategy.waitFor({ timeout: 10000 })
    for (const theme of THEMES) {
      await page.evaluate(v => {
        document.documentElement.dataset.theme = v
      }, theme)
      await page.waitForTimeout(500) // the colours fade between the themes
      await card.locator('[data-screw-optimize-suggest]').scrollIntoViewIfNeeded()
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `no page overflow at ${width}/${theme}`)
      const over = await card.evaluate(c => [...c.querySelectorAll('[data-screw-optimize-suggest], [data-screw-measured-block], [data-screw-proposal-block], [data-screw-measured-table], [data-screw-proposal-block] table, [data-screw-strategy-row]')].filter(e => e.scrollWidth > e.clientWidth + 1).map(e => e.getAttributeNames().find(n => n.startsWith('data-screw')) ?? e.tagName))
      assert.deepEqual(over, [], `nothing is cut off at ${width}/${theme}`)
      const colours = await page.evaluate(() => {
        const root = getComputedStyle(document.documentElement)
        const box = getComputedStyle(document.querySelector('[data-screw-optimize-suggest]'))
        const title = getComputedStyle(document.querySelector('[data-screw-optimize-suggest] p'))
        return { amber: root.getPropertyValue('--amber').trim(), border: box.borderTopColor, title: title.color }
      })
      const probe = await page.evaluate(v => {
        const d = document.createElement('i')
        d.style.color = v
        document.body.append(d)
        const c = getComputedStyle(d).color
        d.remove()
        return c
      }, colours.amber)
      assert.equal(colours.border, probe, `the offer has the warning colour of ${theme}`)
      assert.equal(colours.title, probe, `its title has the warning colour of ${theme}`)
      if (shots && (width === 390 || theme)) {
        const { mkdir } = await import('node:fs/promises')
        await mkdir(shots, { recursive: true })
        if (width === 1440) await card.screenshot({ path: path.join(shots, `schrauben-echt-${theme}.png`) })
      }
    }
    if (width === 390) {
      await page.evaluate(() => {
        document.documentElement.dataset.theme = 'light'
      })
      const ys = await card.locator('[data-screw-measured-row]').evaluateAll(es => es.map(e => Math.round(e.getBoundingClientRect().y)))
      assert.deepEqual([...ys].sort((a, b) => a - b), ys, 'rows in order')
      assert.equal(new Set(ys).size, 4, 'the rows stand one below the other on the phone')
      const small = await card.locator('[data-screw-measured-row], [data-screw-row]').evaluateAll(es => es.filter(e => e.getBoundingClientRect().height < 43.5).map(e => e.getAttribute('data-screw-row') ?? e.getAttribute('data-screw-measured-row')))
      assert.deepEqual(small, [], 'touch targets of at least 44 px on the phone')
      if (shots) await card.screenshot({ path: path.join(shots, 'schrauben-echt-handy-hell.png') })
    }
    void state
    void label2
    await page.close()
  }
  // While a screw measurement runs the values of the last one are dimmed and say so; they are normal again afterwards.
  {
    const { page, card, state } = await open()
    const note = card.locator('[data-screw-running-note]')
    const opacity = sel => card.locator(sel).evaluate(e => getComputedStyle(e).opacity)
    await card.locator('[data-screw-measured-block]').waitFor({ timeout: 10000 })
    assert.equal(await note.count(), 0, 'no note while nothing runs')
    assert.equal(await opacity('[data-screw-measured-block]'), '1')
    const idle = state.preview.console
    state.preview.console = { ...idle, busy: true, completed: false, success: false, generation: idle.generation + 1 }
    await note.waitFor({ timeout: 10000 })
    assert.equal(await text(note), 'A measurement is running. The values below are from the last measurement.')
    for (const sel of ['[data-screw-measured-block]', '[data-screw-proposal-block]']) assert.equal(await opacity(sel), '0.6', `${sel} is dimmed while it runs`)
    assert.equal(await card.locator('[data-screw-measured-row]').count(), 4, 'the last values stay in the table')
    state.preview.console = { ...idle, generation: idle.generation + 2 }
    await page.waitForFunction(() => !document.querySelector('[data-screw-running-note]'), null, { timeout: 10000 })
    assert.equal(await opacity('[data-screw-measured-block]'), '1', 'normal again after the measurement')
    await page.close()
  }
  // The Click Tool can have play when it is put on a screw: its display of the turns says so, the other units do not.
  {
    const { page, card } = await open()
    const note = card.locator('[data-screw-play-note]')
    await card.locator('[data-screw-proposal-block]').waitFor({ timeout: 10000 })
    assert.equal(await note.count(), 0, 'no play note in degrees')
    await page.evaluate(() => localStorage.setItem('cc2-screw-turn-unit', 'clicks'))
    await page.reload()
    await card.locator('[data-screw-proposal-block]').waitFor({ timeout: 10000 })
    await note.waitFor({ timeout: 10000 })
    assert.equal(await text(note), 'The Click Tool still has some play and is being improved. Measure all four corners again after each round.')
    assert.equal(await card.locator('[data-screw-turn-cell="FR"] [data-screw-rotation]').count(), 1, 'the turns are shown in clicks')
    await page.evaluate(() => localStorage.setItem('cc2-screw-turn-unit', 'clock'))
    await page.reload()
    await card.locator('[data-screw-proposal-block]').waitFor({ timeout: 10000 })
    assert.equal(await note.count(), 0, 'no play note on the clock face')
    await page.close()
  }
  // "With mesh" and the choice of the basis cannot be changed while a screw measurement runs; between the measurements of a
  // session and outside a session they are free.
  {
    const { page, card, state } = await open()
    const tick = page.locator('[data-level-mesh-toggle] input')
    const base = page.locator('[data-level-base]')
    await card.locator('[data-screw-measured-block]').waitFor({ timeout: 10000 })
    assert.equal(await tick.isDisabled(), false, 'the tick is free between the measurements of a session')
    assert.equal(await base.isDisabled(), false, 'the choice of the basis is free between the measurements of a session')
    const idle = state.preview.console
    state.preview.console = { ...idle, busy: true, completed: false, success: false, generation: idle.generation + 1 }
    await card.locator('[data-screw-running-note]').waitFor({ timeout: 10000 })
    assert.equal(await tick.isDisabled(), true, 'the tick is locked while a measurement runs')
    assert.equal(await base.isDisabled(), true, 'the choice of the basis is locked while a measurement runs')
    state.preview.console = { ...idle, generation: idle.generation + 2 }
    await page.waitForFunction(() => !document.querySelector('[data-screw-running-note]'), null, { timeout: 10000 })
    assert.equal(await tick.isDisabled(), false, 'the tick is free again after the measurement')
    await page.close()
    const free = await open({ session: false })
    const tick2 = free.page.locator('[data-level-mesh-toggle] input')
    await tick2.waitFor({ timeout: 10000 })
    assert.equal(await tick2.isDisabled(), false, 'the tick is free outside a session')
    await free.page.close()
  }
  // After single corners were measured again the page says that it cannot check whether all four values still fit together.
  {
    const { page, card, state } = await open()
    const note = card.locator('[data-screw-single-note]')
    const z = [1.8, 1.74, 1.72, 1.7]
    await card.locator('[data-screw-measured-block]').waitFor({ timeout: 10000 })
    assert.equal(await note.count(), 0, 'no note after the four-corner measurement')
    state.show(z, [start(z, 10), { time: 12, kind: 'probe', corner: 'RL', twice: false, z: { RL: 1.69 } }])
    await note.waitFor({ timeout: 10000 })
    assert.equal(await text(note), 'Only single corners were measured again (Back left), so the page cannot check whether all four values still fit together. Measure all four corners to be sure.')
    state.show(z, [start(z, 10), { time: 12, kind: 'probe', corner: 'RL', twice: false, z: { RL: 1.69 } }, { time: 14, kind: 'probe', corner: 'RR', twice: false, z: { RR: 1.71 } }])
    await page.waitForFunction(() => document.querySelector('[data-screw-single-note]')?.textContent?.includes('Back right'), null, { timeout: 10000 })
    state.show(z, [start(z, 10), { time: 12, kind: 'probe', corner: 'RL', twice: false, z: { RL: 1.69 } }, { time: 16, kind: 'check', corner: null, twice: false, z: { FL: z[0], FR: z[1], RR: z[2], RL: z[3] } }])
    await page.waitForFunction(() => !document.querySelector('[data-screw-single-note]'), null, { timeout: 10000 })
    await page.close()
  }
  assert.deepEqual(errors, [])
  console.log('PASS screw strategy: standard by default, offer with a button, explicit and stable choice, measured apart, four-corner rule, themes, running note, play note')
} finally {
  await browser?.close()
  await server.close()
}
