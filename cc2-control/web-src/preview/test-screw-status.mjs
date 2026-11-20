// Status and symbols of the screw page (#403): the status of every corner in "Measured" is the verdict of the plan, in the colour of the
// same verdict in the suggestion; no badges, no frames; the strategy label stands in the head of the suggestion; the columns and the
// footer; the reference badge wears the accent colour of the theme; dashes stand on the line of the name.
// (Harness of test-screw-strategy.) // The strategy of the screw plan on the Bed alignment tab (review of #403): front left stays the fixed measurement reference
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
  const open = async ({ viewport = { width: 1440, height: 1000 }, storage = true, empty = false } = {}) => {
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
      data.screws_session = { active: true, xy_homed: true }
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
  const text = async loc => (await loc.innerText()).replace(/\u00a0/g, ' ').trim()
  const cssColor = (page, token) =>
    page.evaluate(n => {
      const d = document.createElement('i')
      d.style.color = `var(${n})`
      document.body.append(d)
      const c = getComputedStyle(d).color
      d.remove()
      return c
    }, token)
  const colorOf = loc => loc.evaluate(e => getComputedStyle(e).color)
  // Every status element in "Measured" has the colour of its row's direction in the suggestion (the same verdict, one table).
  const statusFollowsDirection = async (card, what) => {
    for (const id of ['FR', 'RR', 'RL', 'FL']) {
      const status = card.locator(`[data-screw-verdict="${id}"]`)
      if (!(await status.count())) continue
      const kind = await status.getAttribute('data-screw-verdict-kind')
      const dir = card.locator(`[data-screw-row="${id}"]`).locator(kind === 'ok' ? '[data-screw-ok]' : '[data-screw-direction]')
      assert.equal(await dir.count(), 1, `${what}: ${id} has its direction element (${kind})`)
      if (kind !== 'ok') assert.equal(await dir.getAttribute('data-screw-direction'), kind, `${what}: ${id} direction ${kind}`)
      assert.equal(await colorOf(status), await colorOf(dir), `${what}: status colour of ${id} = colour of its direction (${kind})`)
    }
  }
  const TONE = { ok: '--green', raise: '--red', lower: '--cyan' }
  // Pictures only when CC2_SCREENSHOTS names a folder.
  const snap = async (page, card, name, theme) => {
    if (!shots) return
    await page.evaluate(v => { document.documentElement.dataset.theme = v }, theme)
    await page.waitForTimeout(450)
    // The fixed bars of the app would lie over the picture of a long element: hide them for the shot.
    const hidden = await page.evaluate(() => [...document.querySelectorAll('body *')].filter(e => ['fixed', 'sticky'].includes(getComputedStyle(e).position)).map(e => (e.setAttribute('data-shot-hide', e.style.visibility || '-'), (e.style.visibility = 'hidden'), 1)).length)
    await page.locator('[data-level-step="2"]').screenshot({ path: path.join(shots, `schrauben-echt4-${theme === 'light' ? 'hell' : 'dunkel'}-${name}.png`) })
    await page.evaluate(() => document.querySelectorAll('[data-shot-hide]').forEach(e => { const v = e.getAttribute('data-shot-hide'); e.style.visibility = v === '-' ? '' : v; e.removeAttribute('data-shot-hide') }))
    void hidden
  }

  // 1. Standard plan: FR 60 µm too low (raise), RR 12 µm lower (in order), RL 85 µm too high (lower).
  {
    const { page, card, state } = await open()
    state.show([1.8, 1.74, 1.788, 1.885])
    await card.locator('[data-screw-zheight="RL"]').getByText('1885 µm').waitFor({ timeout: 8000 })
    await card.locator('[data-screw-verdict="RL"]').waitFor()
    const kinds = Object.fromEntries(await Promise.all(['FR', 'RR', 'RL'].map(async id => [id, await card.locator(`[data-screw-verdict="${id}"]`).getAttribute('data-screw-verdict-kind')])))
    assert.deepEqual(kinds, { FR: 'raise', RR: 'ok', RL: 'lower' })
    assert.equal(await text(card.locator('[data-screw-verdict="FR"]')), 'Too low')
    assert.equal(await text(card.locator('[data-screw-verdict="RR"]')), 'Within tolerance')
    assert.equal(await text(card.locator('[data-screw-verdict="RL"]')), 'Too high')
    assert.equal(await card.locator('[data-screw-verdict="FL"]').count(), 0, 'the reference corner has the badge, no status')
    assert.equal(await text(card.locator('[data-screw-measured="FL"]')), 'Reference')
    // "Within tolerance" carries a tick; "Too low" and "Too high" are coloured text without any symbol; the symbol is hidden from the screen reader.
    const glyphs = id => card.locator(`[data-screw-verdict="${id}"] svg`).evaluateAll(a => a.map(s => ({ cls: s.getAttribute('class'), hidden: s.getAttribute('aria-hidden') })))
    assert.equal((await glyphs('FR')).length, 0, 'too low: no symbol')
    assert.equal((await glyphs('RL')).length, 0, 'too high: no symbol')
    const tick = await glyphs('RR')
    assert.equal(tick.length, 1)
    assert.match(tick[0].cls, /lucide-check/)
    assert.equal(tick[0].hidden, 'true')
    // (a) the same verdict in the same colour as the suggestion.
    await statusFollowsDirection(card, 'standard')
    for (const [id, kind] of Object.entries(kinds)) assert.equal(await colorOf(card.locator(`[data-screw-verdict="${id}"]`)), await cssColor(page, TONE[kind]), `${id}: ${kind} wears ${TONE[kind]}`)
    // (b) no frame, no fill, no outline at the status elements; bold text.
    for (const id of ['FR', 'RR', 'RL']) {
      const look = await card.locator(`[data-screw-verdict="${id}"]`).evaluate(e => { const c = getComputedStyle(e); return { b: ['Top', 'Right', 'Bottom', 'Left'].map(s => c[`border${s}Width`]).join(','), bg: c.backgroundColor, o: c.outlineStyle, w: c.fontWeight } })
      assert.deepEqual(look, { b: '0px,0px,0px,0px', bg: 'rgba(0, 0, 0, 0)', o: 'none', w: '600' }, `${id}: no frame and no fill`)
    }
    // The measured deviation in words, independent of the plan: negative lies lower, positive higher, zero nothing.
    const offsetCell = id => card.locator(`[data-screw-measured-row="${id}"] td`).nth(2)
    assert.equal(await text(offsetCell('FL')), '0 µm')
    assert.equal(await text(offsetCell('FR')), '−60 µm (lies lower)')
    assert.equal(await text(offsetCell('RR')), '−12 µm (lies lower)')
    assert.equal(await text(offsetCell('RL')), '+85 µm (lies higher)')
    // (c) the strategy label stands in the head of the suggestion, right of the note on the desktop; not in the head of step 2.
    assert.equal(await page.locator('[data-level-step="2"] > button [data-screw-strategy]').count(), 0, 'not in the head of step 2')
    assert.equal(await page.locator('[data-level-tag]').count(), 0)
    const block = card.locator('[data-screw-proposal-block]')
    assert.equal(await block.locator('[data-screw-proposal-aside] [data-screw-strategy]').count(), 1, 'in the head of the suggestion block')
    assert.equal(await card.locator('[data-screw-strategy]').count(), 1)
    {
      const note = await block.locator('[data-screw-block-note]').boundingBox()
      const tag = await block.locator('[data-screw-strategy]').boundingBox()
      const title = await block.locator('h4').boundingBox()
      assert.ok(tag.x >= note.x + note.width - 1, 'the label stands right of the note')
      assert.ok(Math.abs(tag.y + tag.height / 2 - (note.y + note.height / 2)) < 12, 'on the line of the note')
      assert.ok(note.y + note.height > title.y, 'the note stands in the head of the block, not above its title')
    }
    // Symbols before the titles (Measured: bar chart, Suggestion: wrench), none of them a letter or emoji, all hidden.
    assert.match(await card.locator('[data-screw-measured-block] h4 svg').getAttribute('class'), /lucide-chart-column/)
    assert.match(await block.locator('h4 svg').getAttribute('class'), /lucide-wrench/)
    for (const sel of ['[data-screw-measured-block] h4', '[data-screw-proposal-block] h4'])
      assert.doesNotMatch(await card.locator(sel).innerText(), /\p{Extended_Pictographic}/u, 'no emoji in the titles')
    // (d) the columns; (f) the footer.
    assert.deepEqual(await block.locator('thead th').allInnerTexts().then(a => a.map(x => x.trim())), ['Position', 'Target', 'Direction & amount', 'Turn aid'])
    assert.equal(await card.locator('[data-screw-measured-table] thead th').count(), 4)
    const foot = card.locator('[data-screw-blocks-note]')
    assert.equal(await text(foot), 'Because the blocks are separate, the measured height difference always stays traceable.')
    assert.match(await foot.locator('svg').getAttribute('class'), /lucide-lightbulb/)
    const fb = await foot.boundingBox()
    const bb = await block.boundingBox()
    assert.ok(fb.y >= bb.y + bb.height - 1, 'the footer stands under the suggestion')
    // The targets: standard.
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Leave unchanged')
    for (const id of ['FR', 'RR', 'RL']) assert.equal(await text(card.locator(`[data-screw-goal="${id}"]`)), 'Bring to the height of front left')
    assert.equal(await text(card.locator('[data-screw-adjust="FR"]')), 'Raise 60 µm')
    assert.equal(await text(card.locator('[data-screw-adjust="RL"]')), 'Lower 85 µm')
    await page.close()
  }

  // 1b. The optimized plan: front left is lowered by 60 µm, FR is in order, the others are raised: statuses follow the plan.
  {
    const { page, card, state } = await open()
    state.show([1.8, 1.74, 1.71, 1.68])
    await card.locator('[data-screw-zheight="RL"]').getByText('1680 µm').waitFor({ timeout: 8000 })
    await card.locator('[data-screw-optimize-choose]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    const kinds = Object.fromEntries(await Promise.all(['FR', 'RR', 'RL'].map(async id => [id, await card.locator(`[data-screw-verdict="${id}"]`).getAttribute('data-screw-verdict-kind')])))
    assert.deepEqual(kinds, { FR: 'ok', RR: 'raise', RL: 'raise' }, 'the status follows the plan (front right measured 60 µm low, the plan has nothing left to do there)')
    assert.equal(await text(card.locator('[data-screw-measured-row="FR"] td').nth(2)), '−60 µm (lies lower)', 'the measurement is not changed by the strategy')
    await statusFollowsDirection(card, 'optimized')
    assert.equal(await card.locator('[data-screw-row="FL"] [data-screw-direction="lower"]').count(), 1)
    assert.equal(await card.locator('[data-screw-proposal-block] [data-screw-strategy]').getAttribute('data-screw-strategy'), 'optimized')
    assert.equal(await text(card.locator('[data-screw-goal="FL"]')), 'Move toward the other three corners')
    assert.equal(await text(card.locator('[data-screw-goal="RR"]')), 'Bring to the height of front left (after its move)')
    await page.close()
  }

  // 1c. (decided 20 Nov) One small note under the table of "Measured", only with the optimized plan and only while the plan is shown:
  // the status follows the chosen plan, which also moves front left. Not in the standard plan, not in "Four-corner measurement needed".
  // In that state the strategy label stands in the notice, next to the way back; the suggestion stays hidden.
  const PLAN_NOTE = 'The status follows the chosen plan, which also moves front left.'
  const FL_MOVED = [{ time: 21, kind: 'probe', corner: 'FL', twice: false, z: { FL: 1.812 } }]
  {
    const { page, card, state } = await open()
    state.show([1.8, 1.74, 1.71, 1.68])
    await card.locator('[data-screw-zheight="RL"]').getByText('1680 µm').waitFor({ timeout: 8000 })
    await card.locator('[data-screw-verdict="RL"]').waitFor()
    const note = card.locator('[data-screw-plan-note]')
    assert.equal(await note.count(), 0, '(a) no note with the standard plan')
    assert.equal(await card.locator('[data-screw-mesh-note]').count(), 0, '(a) no mesh line without a mesh')
    await card.locator('[data-screw-optimize-choose]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    assert.equal(await note.count(), 1, '(a) exactly one note with the optimized plan')
    assert.equal(await card.locator('[data-screw-measured-block] [data-screw-plan-note]').count(), 1, '(a) the note stands in the block "Measured"')
    assert.equal(await text(note), PLAN_NOTE)
    for (const th of ['light', 'dark']) await snap(page, card, 'optimiert-plan', th)
    await page.evaluate(() => { document.documentElement.dataset.theme = 'light' })
    assert.equal(await note.locator('svg').count(), 1, '(a) one info symbol')
    assert.equal(await note.locator('svg').getAttribute('aria-hidden'), 'true')
    assert.equal(await note.evaluate(e => getComputedStyle(e).fontSize), '12px', '(a) text-xs')
    assert.equal(await colorOf(note), await cssColor(page, '--muted'), '(a) muted colour')
    const table = await card.locator('[data-screw-measured-table]').boundingBox()
    const nb = await note.boundingBox()
    assert.ok(nb.y >= table.y + table.height - 1, '(a) the note stands under the table')
    assert.equal(await card.locator('[data-screw-measured-row] [data-screw-plan-note]').count(), 0, '(a) not once per row')
    // Standard again: gone.
    await card.locator('[data-screw-strategy-back]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'standard')
    assert.equal(await note.count(), 0, '(a) back on the standard plan the note is gone')
    await page.close()
  }
  {
    const { page, card, state } = await open()
    state.show([1.8, 1.74, 1.71, 1.68])
    await card.locator('[data-screw-zheight="RL"]').getByText('1680 µm').waitFor({ timeout: 8000 })
    await card.locator('[data-screw-optimize-choose]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    state.show([1.812, 1.74, 1.71, 1.68], [start([1.8, 1.74, 1.71, 1.68]), ...FL_MOVED])
    const needed = card.locator('[data-screw-four-needed]')
    await needed.waitFor({ timeout: 8000 })
    assert.equal(await card.locator('[data-screw-plan-note]').count(), 0, '(a) no note in the state "Four-corner measurement needed"')
    assert.equal(await card.locator('[data-screw-proposal-block]').count(), 0, 'the suggestion stays hidden')
    const tag = needed.locator('[data-screw-strategy="optimized"]')
    assert.equal(await tag.count(), 1, '(b) the strategy label stands in the notice')
    assert.match(await text(tag), /Optimized/, '(b) the label says Optimized')
    assert.equal(await tag.locator('svg').count(), 1, '(b) with its symbol, as in the suggestion head')
    const back = card.locator('[data-screw-strategy-back]')
    assert.equal(await back.count(), 1, '(b) the way back stays')
    assert.equal(await text(back), 'Back to standard')
    assert.ok(await back.isVisible(), '(b) the way back is visible')
    assert.equal(await card.locator('[data-screw-strategy]').count(), 1, 'one label only')
    for (const th of ['light', 'dark']) await snap(page, card, 'vier-ecken', th)
    await page.evaluate(() => { document.documentElement.dataset.theme = 'light' })
    // Same look as in the head of the suggestion: warning tone, round tag.
    const look = await tag.evaluate(e => { const c = getComputedStyle(e); return [c.color, c.borderTopColor, c.borderRadius].join('|') })
    assert.equal(look.split('|')[0], await cssColor(page, '--amber'), '(b) warning colour of the label')
    // Back on the standard plan the notice is gone and with it the label; the suggestion returns.
    await back.click()
    await page.waitForFunction(() => document.querySelector('[data-screw-four-needed]') === null)
    assert.equal(await card.locator('[data-screw-plan-note]').count(), 0)
    await page.close()
  }
  // (c) Phone and desktop, seven themes: the note and the label run over nothing and stay on the screen.
  for (const [width, label] of [[1440, 'desktop'], [390, 'phone']]) {
    const { page, card, state } = await open({ viewport: { width, height: 1100 } })
    state.show([1.8, 1.74, 1.71, 1.68])
    await card.locator('[data-screw-zheight="RL"]').getByText('1680 µm').waitFor({ timeout: 8000 })
    await card.locator('[data-screw-optimize-choose]').click()
    await page.waitForFunction(() => document.querySelector('[data-screw-strategy]')?.getAttribute('data-screw-strategy') === 'optimized')
    const check = async what => {
      for (const theme of THEMES) {
        await page.evaluate(v => { document.documentElement.dataset.theme = v }, theme)
        await page.waitForTimeout(450)
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `(c) no page overflow at ${label}/${what}/${theme}`)
        const over = await card.evaluate(c => [...c.querySelectorAll('[data-screw-measured-block], [data-screw-plan-note], [data-screw-four-needed], [data-screw-strategy-row], [data-screw-strategy]')].filter(e => e.scrollWidth > e.clientWidth + 1).map(e => e.getAttributeNames().find(n => n.startsWith('data-screw')) ?? e.tagName))
        assert.deepEqual(over, [], `(c) nothing is cut off at ${label}/${what}/${theme}`)
        for (const sel of ['[data-screw-plan-note]', '[data-screw-four-needed] [data-screw-strategy]']) {
          const l = card.locator(sel)
          if (!(await l.count())) continue
          const b = await l.boundingBox()
          assert.ok(b.x >= 0 && b.x + b.width <= width + 1, `(c) ${sel} stays on the screen at ${label}/${what}/${theme}`)
        }
        if (what === 'note') assert.equal(await colorOf(card.locator('[data-screw-plan-note]')), await cssColor(page, '--muted'), `(c) muted note at ${theme}`)
      }
    }
    assert.equal(await card.locator('[data-screw-plan-note]').count(), 1)
    await check('note')
    state.show([1.812, 1.74, 1.71, 1.68], [start([1.8, 1.74, 1.71, 1.68]), ...FL_MOVED])
    await card.locator('[data-screw-four-needed]').waitFor({ timeout: 8000 })
    await check('four-needed')
    if (width === 390) await snap(page, card, 'vier-ecken-handy', 'light')
    await page.close()
  }

  // 2. Seven themes: the reference badge is filled with the accent colour of the theme (the colour of the primary button, no colour of
  // its own) with the ink of the theme; the statuses wear the theme's green, red and cyan; nothing runs over.
  for (const [width, label] of [[1440, 'desktop'], [390, 'phone']]) {
    const { page, card, state } = await open({ viewport: { width, height: 1100 } })
    state.show([1.8, 1.74, 1.788, 1.885])
    await card.locator('[data-screw-verdict="RL"]').waitFor({ timeout: 8000 })
    for (const theme of THEMES) {
      await page.evaluate(v => { document.documentElement.dataset.theme = v }, theme)
      await page.waitForTimeout(450)
      const badge = card.locator('[data-screw-measured="FL"]')
      const look = await badge.evaluate(e => { const c = getComputedStyle(e); return { bg: c.backgroundColor, fg: c.color, bw: c.borderTopWidth } })
      assert.equal(look.bg, await cssColor(page, '--cyan'), `${label}/${theme}: the badge is filled with the accent colour`)
      assert.equal(look.fg, await cssColor(page, '--ink'), `${label}/${theme}: ink on the accent`)
      assert.equal(look.bw, '0px')
      const primary = await page.locator('[data-level-measure-again]').evaluate(e => getComputedStyle(e).backgroundColor).catch(() => null)
      if (primary) assert.equal(look.bg, primary, `${label}/${theme}: the colour of the primary button`)
      assert.notEqual(look.bg, await cssColor(page, '--blue'), 'not the blue token')
      for (const [id, tone] of [['FR', '--red'], ['RR', '--green'], ['RL', '--cyan']]) assert.equal(await colorOf(card.locator(`[data-screw-verdict="${id}"]`)), await cssColor(page, tone), `${label}/${theme}: ${id}`)
      await statusFollowsDirection(card, `${label}/${theme}`)
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `(g) no page overflow at ${label}/${theme}`)
      const over = await card.evaluate(c => [...c.querySelectorAll('[data-screw-measured-block], [data-screw-proposal-block], [data-screw-measured-table], [data-screw-proposal-table], [data-screw-blocks-note]')].filter(e => e.scrollWidth > e.clientWidth + 1).map(e => e.getAttributeNames().find(n => n.startsWith('data-screw')) ?? e.tagName))
      assert.deepEqual(over, [], `nothing is cut off at ${label}/${theme}`)
    }
    if (width === 390) {
      await page.evaluate(() => { document.documentElement.dataset.theme = 'light' })
      const block = card.locator('[data-screw-proposal-block]')
      const note = await block.locator('[data-screw-block-note]').boundingBox()
      const tag = await block.locator('[data-screw-strategy]').boundingBox()
      assert.ok(tag.y >= note.y + note.height - 1, 'on the phone the label stands under the line of the note')
      assert.ok(tag.x + tag.width <= 390 + 1, 'the label stays on the screen')
      const small = await card.locator('[data-screw-measured-row], [data-screw-row]').evaluateAll(es => es.filter(e => e.getBoundingClientRect().height < 43.5).map(e => e.getAttribute('data-screw-row') ?? e.getAttribute('data-screw-measured-row')))
      assert.deepEqual(small, [], 'touch targets of at least 44 px')
      const cut = await card.evaluate(c => [...c.querySelectorAll('[data-screw-verdict], [data-screw-goal], [data-screw-adjust], [data-screw-turn-cell], [data-screw-blocks-note]')].filter(e => e.scrollWidth > e.clientWidth + 1).map(e => e.getAttributeNames().find(n => n.startsWith('data-screw'))))
      assert.deepEqual(cut, [], 'no cell is cut off on the phone')
    }
    await page.close()
  }

  // 3. (h) Before the first measurement a dash stands on the line of the name in both tables.
  {
    const { page, card } = await open({ empty: true })
    await card.locator('[data-screw-proposal-block]').waitFor({ timeout: 10000 })
    for (const sel of ['[data-screw-measured-table]', '[data-screw-proposal-table]']) {
      const offs = await card.locator(`${sel} tbody tr`).evaluateAll(rows =>
        rows.map(r => {
          const centre = node => { const g = document.createRange(); g.selectNodeContents(node); const b = g.getBoundingClientRect(); return b.y + b.height / 2 }
          const textNodes = el => { const w = document.createTreeWalker(el, NodeFilter.SHOW_TEXT); const a = []; while (w.nextNode()) a.push(w.currentNode); return a }
          const cells = [...r.children]
          const names = textNodes(cells[0]).filter(n => n.textContent.trim())
          const name = centre(names[names.length - 1])
          const dashes = cells.slice(1).flatMap(c => textNodes(c).filter(n => n.textContent.trim() === '—'))
          return { dashes: dashes.length, worst: Math.max(0, ...dashes.map(n => Math.abs(centre(n) - name))) }
        })
      )
      for (const o of offs) {
        assert.ok(o.dashes >= 1, `${sel}: dashes before the first measurement`)
        assert.ok(o.worst <= 1.5, `${sel}: a dash is ${o.worst.toFixed(1)} px off the line of the name`)
      }
    }
    assert.equal(await card.locator('[data-screw-verdict]').count(), 0, 'no status before a measurement')
    await page.close()
  }
  assert.deepEqual(errors, [])
  console.log('PASS screw status: verdict and colour shared by status and suggestion, no frames, label in the suggestion head, columns, footer, accent badge in 7 themes, phone, dashes')
} finally {
  await browser?.close()
  await server.close()
}
