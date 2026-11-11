import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
const out = process.env.CC2_SCREENSHOTS || '/opt/cursor/artifacts'
const themes = ['dark', 'light']
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({
  root,
  configFile: path.join(root, 'vite.config.ts'),
  mode: 'demo',
  server: { port: 0, host: '127.0.0.1' },
})
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH
      ? {
          executablePath: process.env.CC2_BROWSER_PATH,
          args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer'],
        }
      : {}),
  })
  await mkdir(out, { recursive: true })
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } })
  await page.goto(`${origin}/#job`)
  await page.getByRole('button', { name: 'Expand bed map', exact: true }).click()
  const dialog = page.getByRole('dialog')
  await dialog.getByRole('heading', { name: 'Bed map', exact: true }).waitFor()
  await dialog.getByRole('button', { name: 'Layer path', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('[data-layer-path]').length > 0)
  const copy2 = dialog.locator('[data-bed-list-id="Copy 2"]')
  if (await copy2.count()) await copy2.click()
  await page.locator('#cc2-preview-banner').evaluate(el => {
    el.style.display = 'none'
  })
  const saved = []
  for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: width === 390 ? 844 : 900 })
    for (const theme of themes) {
      await page.evaluate(id => {
        document.documentElement.dataset.theme = id
      }, theme)
      await page.waitForTimeout(200)
      const name = `layer-path-demo-${theme}-${width === 1440 ? 'desktop' : 'phone'}.png`
      await page.screenshot({ path: path.join(out, name) })
      saved.push(name)
    }
  }
  console.log(`PASS: ${saved.length} demo screenshots\n${saved.join('\n')}`)
} finally {
  await browser?.close()
  await server.close()
}
