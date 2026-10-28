import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0]
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.goto(origin + '#settings/appearance')
  const theme = page.locator('select').filter({ has: page.locator('option[value="obsidian"]') })
  await theme.waitFor()
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'dark')
  assert.deepEqual(await theme.locator('option').evaluateAll(options => options.map(option => option.value)), ['dark', 'light', 'dracula', 'nord', 'monokai', 'solarized-light', 'obsidian'])
  const saved = page.waitForResponse(r => r.url().endsWith('/api/preferences') && r.request().method() === 'PUT')
  await theme.selectOption('obsidian')
  assert.equal((await saved).ok(), true)
  await page.reload()
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'obsidian')
  assert.equal(await theme.inputValue(), 'obsidian')
  const fresh = await browser.newContext()
  const other = await fresh.newPage()
  await other.goto(origin + '#settings/appearance')
  await other.waitForFunction(() => document.documentElement.dataset.theme === 'obsidian')
  await fresh.close()
  const box = () => page.evaluate(() => {
    const content = document.querySelector('.cc2-page-content').getBoundingClientRect()
    const side = document.querySelector('#cc2-navigation').getBoundingClientRect()
    const main = document.querySelector('main').getBoundingClientRect()
    const camera = document.querySelector('.cc2-camera-frame')?.getBoundingClientRect()
    return { width: content.width, centre: content.x + content.width / 2, expectedCentre: (side.right + main.right) / 2, camera: camera?.width, overflow: document.documentElement.scrollWidth > innerWidth }
  })
  for (const width of [1024, 1440, 1920, 2560]) {
    await page.setViewportSize({ width, height: 1100 })
    for (const route of ['dashboard', 'control', 'canvas', 'settings/appearance']) {
      await page.goto(origin + '#' + route)
      await page.waitForTimeout(350)
      const before = await box()
      await page.getByTitle('Collapse menu', { exact: true }).click()
      await page.waitForTimeout(350)
      const after = await box()
      assert(Math.abs(before.width - after.width) < 1, `${route} at ${width}: content changed width: ${before.width} -> ${after.width}`)
      if (before.camera) assert(Math.abs(before.camera - after.camera) < 1, 'camera changed width')
      assert(Math.abs(after.centre - after.expectedCentre) < 1, `${route}: content not centred`)
      assert.equal(after.overflow, false)
      await page.getByTitle('Expand menu', { exact: true }).click()
      await page.waitForTimeout(350)
    }
  }
  for (const width of [390, 768]) {
    await page.setViewportSize({ width, height: 1100 })
    await page.goto(origin + '#dashboard')
    await page.waitForTimeout(350)
    const before = await box()
    await page.locator('#cc2-menu-toggle').click()
    await page.waitForTimeout(200)
    assert(Math.abs((await box()).width - before.width) < 1, 'drawer resized mobile page')
    await page.keyboard.press('Escape')
    assert.equal((await box()).overflow, false)
  }
  for (const id of ['light', 'dark', 'obsidian']) {
    await page.goto(origin + '#settings/appearance')
    const saved = page.waitForResponse(r => r.url().endsWith('/api/preferences') && r.request().method() === 'PUT')
    await theme.selectOption(id)
    await saved
    await page.reload()
    await page.waitForFunction(value => document.documentElement.dataset.theme === value, id)
  }
  assert.deepEqual(errors, [])
  console.log('PASS: Obsidian selection/server persistence, theme switching, desktop page/camera sizing and centring, mobile drawer')
} finally { await browser?.close(); await server.close() }
