// The numbered marks (1-4) of the screw page are exact circles with the digit in the middle: the "Measured" and
// "Suggestion" tables, the top view of the plate and the step badge. Measured with getBoundingClientRect (width == height,
// tolerance 0.5 px) and with the ink of the digit in a screenshot of the mark (centre within 0.5 px of the circle's centre),
// in the 7 themes, on the desktop and at 390 px. Probe values are synthetic.
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
const THEMES = ['light', 'dark', 'dracula', 'nord', 'monokai', 'solarized-light', 'obsidian']
const errors = []
const report = []
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
  const fulfill = body => ({ contentType: 'application/json', body: JSON.stringify(body) })
  const open = async (viewport, touch = false) => {
    const page = await browser.newPage({ viewport, deviceScaleFactor: 2, ...(touch ? { hasTouch: true, isMobile: true } : {}) })
    page.on('pageerror', e => errors.push(e.message))
    await page.addInitScript(() => {
      try {
        localStorage.setItem('cc2-level-base', JSON.stringify({ choice: 'custom', temp: '60' }))
        localStorage.setItem('cc2-screw-turn-mode', 'exact')
      } catch {}
    })
    const z = [1.8, 1.74, 1.72, 1.7]
    const preview = {
      console: { busy: false, completed: true, success: true, generation: 101, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n', output: tramOutput(heightsOf(z)) },
      history: [{ started: 10, ended: null, measurements: [{ time: 10, kind: 'start', corner: null, twice: false, z: { FL: z[0], FR: z[1], RR: z[2], RL: z[3] } }] }],
    }
    await page.route(/\/api\/console(\?.*)?$/, route => (route.request().method() === 'GET' ? route.fulfill(fulfill(preview.console)) : route.fallback()))
    await page.route('**/api/printer', async route => {
      const data = await (await route.fetch()).json()
      data.screws_session = { active: true, xy_homed: true }
      return route.fulfill({ json: data })
    })
    await page.route('**/api/screws/history', route => (route.request().method() === 'GET' ? route.fulfill(fulfill({ sessions: preview.history })) : route.fallback()))
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
    await card.locator('[data-screw-zheight="FR"]').waitFor({ timeout: 10000 })
    return { page, card }
  }
  // Every numbered mark in the card: a leaf with the text 1-4 and a round shape (class rounded-full, or the data mark).
  const MARKS = `#screwFocus :is(span,b,button,i)`
  const marksOf = () =>
    [...document.querySelectorAll('#screwFocus span,#screwFocus b,#screwFocus button,#screwFocus i')]
      .filter(e => e.children.length === 0 && /^[1-4]$/.test(e.textContent.trim()) && (e.hasAttribute('data-num-mark') || /rounded-full/.test(e.className)))
      .map((e, i) => {
        e.setAttribute('data-test-mark', String(i))
        const r = e.getBoundingClientRect()
        const area = e.closest('[data-screw-measured-block]') ? 'measured' : e.closest('[data-screw-proposal-block]') ? 'proposal' : e.closest('[data-screw-pick-mark]') || e.hasAttribute('data-screw-pick-mark') ? 'top view' : e.closest('[data-level-step]')?.hasAttribute('data-level-step') && !e.closest('table') ? 'step/other' : 'other'
        const cs = getComputedStyle(e)
        return { i, area, text: e.textContent.trim(), w: r.width, h: r.height, border: cs.borderTopWidth, radius: cs.borderTopLeftRadius, font: cs.fontSize }
      })
  const inkOffset = async (page, i) => {
    const el = page.locator(`[data-test-mark="${i}"]`)
    await el.evaluate(e => e.scrollIntoView({ block: 'center' })) // away from the sticky bars
    await page.waitForTimeout(30)
    const covered = await el.evaluate(e => { const r = e.getBoundingClientRect(); const at = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2); return !(at === e || e.contains(at)) })
    if (covered) return null
    const png = (await el.screenshot()).toString('base64')
    return page.evaluate(async b64 => {
      const bytes = Uint8Array.from(atob(b64), ch => ch.charCodeAt(0))
      const bmp = await createImageBitmap(new Blob([bytes], { type: 'image/png' }))
      const c = new OffscreenCanvas(bmp.width, bmp.height)
      const g = c.getContext('2d', { willReadFrequently: true })
      g.drawImage(bmp, 0, 0)
      const { data, width, height } = g.getImageData(0, 0, bmp.width, bmp.height)
      const px = (x, y) => [data[(y * width + x) * 4], data[(y * width + x) * 4 + 1], data[(y * width + x) * 4 + 2]]
      const bg = px(Math.round(width * 0.2), Math.round(height / 2)) // inside the circle, left of the digit
      const cx = width / 2
      const cy = height / 2
      const rIn = width / 2 - 6 // inside the border (device pixels at scale 2)
      let x0 = 1e9, x1 = -1, y0 = 1e9, y1 = -1
      for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
          if ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 > rIn * rIn) continue
          const p = px(x, y)
          if (Math.abs(p[0] - bg[0]) + Math.abs(p[1] - bg[1]) + Math.abs(p[2] - bg[2]) > 120) {
            x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y)
          }
        }
      if (x1 < 0) return null
      return { dx: ((x0 + x1 + 1) / 2 - cx) / 2, dy: ((y0 + y1 + 1) / 2 - cy) / 2 }
    }, png)
  }

  const problems = []
  for (const [width, label, touch] of [[1440, 'desktop', false], [390, 'phone', false], [390, 'phone-touch', true]]) {
    const { page, card } = await open({ width, height: 1100 }, touch)
    if (touch) assert.ok(await page.evaluate(() => matchMedia('(pointer: coarse)').matches), 'a coarse pointer (touch screen)')
    for (const theme of THEMES) {
      await page.evaluate(v => {
        document.documentElement.dataset.theme = v
      }, theme)
      await page.waitForTimeout(500)
      const marks = await page.evaluate(marksOf)
      const areas = marks.reduce((m, x) => ({ ...m, [x.area]: (m[x.area] ?? 0) + 1 }), {})
      for (const need of ['measured', 'proposal', 'top view']) if (!areas[need]) problems.push(`${label}/${theme}: no numbered marks found in ${need} (${JSON.stringify(areas)})`)
      for (const m of marks) {
        const sizes = `${m.area} ${m.text}: ${m.w.toFixed(2)} x ${m.h.toFixed(2)} (border ${m.border}, font ${m.font})`
        if (Math.abs(m.w - m.h) > 0.5) problems.push(`${label}/${theme} not round: ${sizes}`)
        const ink = await inkOffset(page, m.i)
        if (ink && (Math.abs(ink.dx) > 0.5 || Math.abs(ink.dy) > 0.5)) problems.push(`${label}/${theme} digit off centre: ${m.area} ${m.text} dx ${ink.dx.toFixed(2)} dy ${ink.dy.toFixed(2)}`)
        if (theme === 'light') report.push(`${label}/${theme} ${sizes}${ink ? ` ink dx ${ink.dx.toFixed(2)} dy ${ink.dy.toFixed(2)}` : ''}`)
      }
      // all marks in one area share one size, and so do all marks of the page
      const sizes = new Set(marks.map(m => `${Math.round(m.w * 10) / 10}`))
      if (sizes.size > 1) problems.push(`${label}/${theme}: the marks have different sizes: ${[...sizes].join(', ')}`)
    }
    await page.close()
  }
  console.log(report.join('\n'))
  assert.deepEqual(problems, [], `numbered marks are not exact circles:\n${problems.join('\n')}`)
  assert.deepEqual(errors, [])
  console.log('PASS screw number marks: width == height, digit centred, 7 themes, desktop and 390 px')
} finally {
  await browser?.close()
  await server.close()
}
