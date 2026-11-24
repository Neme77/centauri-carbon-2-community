// Pictures of the real screw table (A2): dark, German, 390 and 1440 px, micrometres and millimetres.
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { readFile } from 'node:fs/promises'
import { createServer } from 'vite'
import { chromium } from 'playwright'
const out = process.argv[2]
const root0 = fileURLToPath(new URL('..', import.meta.url))
const german = JSON.parse(await readFile(path.join(root0, 'public/locales/de.json'), 'utf8'))
const lines = h => Object.entries(h).map(([k, z]) => { const [x, y] = k.split(','); const v = Number(z).toFixed(6); return `${`// probe at ${Number(x).toFixed(3)},${Number(y).toFixed(3)} is z=${v}\n`.repeat(3)}// Result is z=${v}\n` }).join('')
// the values of the accepted pictures (fassung-A2-final-*): front left -655 µm is the target
const sets = { normal: { '27.9,25.95': -0.655, '227.15,25.95': -0.868, '227.15,225.5': -0.56, '27.9,225.5': -0.69 }, gross: { '27.9,25.95': -0.655, '227.15,25.95': -2.155, '227.15,225.5': 0.145, '27.9,225.5': -1.215 } }
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
await server.listen(); await mkdir(out, { recursive: true })
const browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}) })
const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
for (const [w, h] of [[1440, 1100], [390, 844]]) for (const [name, heights] of Object.entries(sets)) for (const unit of ['um', 'mm']) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h }, colorScheme: 'dark', locale: 'de-DE' })
  await ctx.addInitScript(([u]) => { try { localStorage.setItem('cc2-language', 'de'); localStorage.setItem('cc2-theme', 'dark'); localStorage.setItem('cc2-screw-height-unit', u) } catch {} }, [unit])
  const page = await ctx.newPage()
  await page.route('**/api/console', r => r.request().method() === 'GET' ? r.fulfill({ json: { busy: false, completed: true, success: true, generation: 70, command: 'SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\n', output: `SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n${lines(heights)}` } }) : r.fallback())
  await page.route('**/i18n/de.json', r => r.fulfill({ json: german }))
  await page.route('**/api/preferences', r => r.request().method() === 'GET' ? r.fulfill({ json: { language: 'de', theme: 'dark' } }) : r.fallback())
  await page.goto(origin); await page.locator('#cc2-preview-banner').waitFor()
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-preview-banner').waitFor()
  await page.goto(`${origin}/#bed/screws`); await page.reload()
  const t = page.locator('#screwFocus table'); await t.waitFor({ timeout: 10000 })
  if ((await page.evaluate(() => document.documentElement.lang)) !== 'de') {
    await page.locator('main select:has(option[value="de"])').first().selectOption('de')
    await page.waitForFunction(() => document.documentElement.lang === 'de')
  }
  await page.locator('#cc2-preview-banner').evaluate(e => (e.style.display = 'none')).catch(() => {})
  const block = page.locator('#screwFocus [data-screw-lists]').locator('xpath=..')
  await block.screenshot({ path: path.join(out, `a2-neu-${w}-${name}-${unit}.png`) })
  console.log(w, name, unit, 'ok', await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1))
  await ctx.close()
}
await browser.close(); await server.close()
