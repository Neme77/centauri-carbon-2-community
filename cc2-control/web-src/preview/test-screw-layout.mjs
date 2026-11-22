// Screw page, narrow screens: nothing of the table is cut off, nothing sticks out of its cell, the page itself never
// scrolls sideways. Phone width (390 px) in normal and large text, in the languages with the longest words.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const KEYS = ['27.9,25.95', '227.15,25.95', '227.15,225.5', '27.9,225.5']
const heights = ['0.000000', '-0.213000', '0.095000', '-0.560000']
const output = KEYS.map((key, i) => {
  const [x, y] = key.split(',')
  return `${`// probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${heights[i]}\n`.repeat(3)}// Result is z=${heights[i]}\n`
}).join('')
const job = {
  busy: false,
  completed: true,
  success: true,
  generation: 70,
  command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n',
  output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${output}`,
}
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
  logLevel: 'error',
})
let browser
const problems = []
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
  const page = await browser.newPage({ viewport: { width: 390, height: 844 } })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  let lang = 'de'
  await page.route(/\/api\/console(\?.*)?$/, route => (route.request().method() === 'GET' ? route.fulfill({ json: job }) : route.fallback()))
  await page.route('**/api/preferences', route =>
    route.request().method() === 'GET' ? route.fulfill({ json: { language: lang, theme: 'dark' } }) : route.fulfill({ json: {} })
  )
  for (const l of ['de', 'it', 'fr', 'es', 'hu', 'ru']) {
    lang = l
    await page.goto(origin)
    await page.locator('#cc2-preview-banner').waitFor()
    await page.evaluate(v => localStorage.setItem('cc2-language', v), l)
    if ((await page.locator('#preview-scene').inputValue()) !== 'idle')
      await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
    await page.goto(`${origin}/#bed/align`)
    await page.reload()
    await page.locator('#screwFocus [data-screw-measured="FL"]').waitFor({ timeout: 10000 })
    for (const font of [100, 150]) {
      await page.evaluate(f => (document.documentElement.style.fontSize = f === 100 ? '' : `${f}%`), font)
      await page.waitForTimeout(60)
      const m = await page.evaluate(() => {
        const card = document.querySelector('#screwFocus')
        const table = card.querySelector('table')
        const tb = table.getBoundingClientRect()
        const cells = [...table.querySelectorAll('th,td,[data-screw-measured],[data-screw-target]')]
        const cut = cells.filter(c => c.scrollWidth > c.clientWidth + 1 && getComputedStyle(c).display !== 'inline')
        const out = [...table.querySelectorAll('*')].filter(e => {
          const b = e.getBoundingClientRect()
          return b.width && (b.right > tb.right + 1 || b.left < tb.left - 1)
        })
        const select = [...card.querySelectorAll('[data-screw-lists] select')].filter(s => {
          const cs = getComputedStyle(s)
          return s.scrollWidth > s.clientWidth + 1 && cs.display !== 'none'
        })
        return {
          page: Math.max(0, document.documentElement.scrollWidth - innerWidth),
          cut: cut.map(c => c.innerText.slice(0, 30)),
          out: out.map(e => e.innerText?.slice(0, 30) ?? e.tagName),
          select: select.length,
        }
      })
      if (m.page > 1 || m.cut.length || m.out.length) problems.push(`${l} ${font}%: ${JSON.stringify(m)}`)
    }
    await page.evaluate(() => (document.documentElement.style.fontSize = ''))
  }
} finally {
  await browser?.close()
  await server.close()
}
assert.deepEqual(problems, [], `the table is cut off or the page scrolls sideways:\n${problems.join('\n')}`)
console.log('screw layout 390 px: ok')
