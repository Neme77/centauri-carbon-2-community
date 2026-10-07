import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true })
  const page = await browser.newPage()
  let sequence = 1
  let connected = true
  let level = 2
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async route => {
    const response = await route.fetch()
    const data = await response.json()
    data.connected = connected
    data.printer_report = { event_id: `test-${sequence}`, sequence, code: 1264, level, message: 'Filament clog <script>window.injected=1</script>', age: 5 }
    await route.fulfill({ json: data })
  })
  await page.goto(server.resolvedUrls.local[0])
  const alert = page.getByTestId('printer-report')
  await alert.waitFor()
  assert.match(await alert.innerText(), /Code 1264/)
  assert.match(await alert.innerText(), /Filament clog <script>/)
  assert.equal(await page.evaluate(() => window.injected), undefined)
  await page.waitForTimeout(8000)
  assert.equal(await alert.count(), 1, 'report has no toast timeout')
  connected = false
  await alert.getByText('Printer disconnected; the last report is retained.').waitFor()
  await alert.getByRole('button', { name: 'Dismiss this report' }).click()
  assert.equal(await alert.count(), 0)
  await page.reload()
  await page.waitForTimeout(4000)
  assert.equal(await alert.count(), 0, 'refresh must not replay an acknowledged event')
  sequence++
  await alert.waitFor()
  level = 3; sequence++
  await page.waitForTimeout(4000)
  assert.equal(await alert.count(), 0, 'resume is not an alarm')
  level = 1; sequence++
  await alert.waitFor()
  console.log('PASS: native reports persist, render as text, survive disconnect and can be dismissed')
} finally {
  await browser?.close()
  await server.close()
}
