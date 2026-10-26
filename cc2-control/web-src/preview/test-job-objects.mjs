import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// The Job page asks for objects only during a print: the printer firmware keeps every request
// CC2 Control sends on its local socket in memory, so an idle Job page must send none.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  let objectRequests = 0
  page.on('request', r => { if (new URL(r.url()).pathname === '/api/exclude-objects') objectRequests++ })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))

  await page.goto(`${origin}/#job`)
  await page.selectOption('#preview-scene', 'idle')
  await page.waitForLoadState('load')
  await page.getByText('Objects will appear during a supported print.').waitFor()
  objectRequests = 0
  await page.waitForTimeout(5000)
  assert.equal(objectRequests, 0, 'an idle Job page does not ask for objects')

  await page.selectOption('#preview-scene', 'printing')
  await page.waitForLoadState('load')
  await page.getByRole('button', { name: /Buddha/ }).first().waitFor()
  assert.ok(objectRequests > 0, 'a printing Job page asks for objects')
  assert.deepEqual(errors, [])
  console.log('PASS: the Job page asks for objects only during a print')
} finally {
  await browser?.close()
  await server.close()
}
