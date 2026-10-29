import assert from 'node:assert/strict'
import { readFile, mkdir, writeFile } from 'node:fs/promises'
import { createRequire } from 'node:module'
import { pathToFileURL } from 'node:url'
import path from 'node:path'

const app = path.resolve(process.argv[2]), output = path.resolve(process.argv[3])
const web = path.join(app, 'web-src'), require = createRequire(path.join(web, 'package.json'))
const { createServer } = await import(pathToFileURL(require.resolve('vite')).href)
const { chromium } = require('playwright')
const axe = await readFile(process.env.CC2_AXE_PATH, 'utf8')
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root: web, configFile: path.join(web, 'vite.config.ts'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser
const reports = []
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu'] } : {}) })
  const context = await browser.newContext()
  await context.route('**/api/network', route => route.fulfill({ json: {
    available: true, fresh: true, age_ms: 0, snapshot: { schema: 1,
      wifi: { present: true, enabled: true, carrier: true, address: '192.0.2.103' },
      ethernet: { present: false, enabled: false, carrier: null, address: null },
      wireless: { control_available: true, state: 'COMPLETED', ssid: 'Demo network', security: 'WPA2-PSK', frequency_mhz: 5220, signal_dbm: -47 },
      route: { selected: 'wifi' } } } }))
  const page = await context.newPage()
  const pages = ['dashboard', 'control', 'job', 'files', 'history', 'bed', 'canvas', 'spools', 'console',
    'settings/connection', 'settings/integrations', 'settings/appearance', 'settings/about', 'settings/updates', 'settings/backup']
  for (const theme of ['dark', 'light']) for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.goto(origin + '/#settings/appearance')
    await page.locator('select').filter({ has: page.locator('option[value="obsidian"]') }).selectOption(theme)
    for (const route of pages) {
      await page.goto(origin + '/#' + route)
      await page.waitForTimeout(300)
      if (route === 'settings/connection') {
        const toggle = page.getByRole('switch', { name: 'Show Ethernet options' })
        if (await toggle.getAttribute('aria-checked') === 'false') await toggle.click()
      }
      await page.addScriptTag({ content: axe })
      const result = await page.evaluate(async () => {
        const scan = await window.axe.run(document, { runOnly: { type: 'tag', values: ['wcag2a', 'wcag2aa', 'wcag21aa'] } })
        return scan.violations.map(v => ({ id: v.id, impact: v.impact, nodes: v.nodes.map(n => n.target) }))
      })
      reports.push({ route, theme, width, violations: result })
    }
  }
  await mkdir(output, { recursive: true })
  await writeFile(path.join(output, 'accessibility.json'), JSON.stringify(reports, null, 2))
  assert.deepEqual(reports.filter(r => r.violations.length), [])
  console.log('PASS: axe WCAG A/AA on 15 routes, desktop/phone, dark/light')
} finally { await browser?.close(); await server.close() }
