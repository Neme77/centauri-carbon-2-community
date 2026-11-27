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
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  const writes = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.url().includes('/api/appearance') && r.method() !== 'GET') writes.push({ url: r.url(), method: r.method(), body: r.postData() })
  })
  const origin = server.resolvedUrls.local[0]
  await page.goto(origin + '#appearance/themes')
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  if (process.env.CC2_SCREENSHOTS) {
    await page.waitForFunction(() => document.querySelector('#cc2-navigation')?.getBoundingClientRect().width > 200)
    await page.addStyleTag({ content: '#cc2-preview-banner{display:none!important}' })
    await page.waitForTimeout(200)
    await page.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, 'appearance-themes.png'), fullPage: true })
  }

  // A built-in theme applies immediately and is written to the printer.
  const nord = page.locator('button[aria-pressed]').filter({ hasText: 'Nord' }).first()
  const nordWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance') && r.method() === 'PUT')
  await nord.click()
  await nordWrite
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'nord')
  assert(writes.some(w => w.method === 'PUT' && w.body.includes('"theme":"nord"')), JSON.stringify(writes))

  // A custom theme starts from a preset, saves, and applies its tokens inline.
  await page.getByRole('button', { name: 'New theme', exact: true }).click()
  const dialog = page.getByRole('dialog')
  await dialog.locator('select').selectOption('klipper-dark')
  await dialog.locator('input').first().fill('Demo industrial')
  await dialog.locator('#token-cyan').evaluate(el => {
    el.value = '#ff00aa'
    el.dispatchEvent(new Event('input', { bubbles: true }))
  })
  const themeWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance') && r.method() === 'PUT')
  await dialog.getByRole('button', { name: 'Save theme', exact: true }).click()
  await themeWrite
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'custom')
  const custom = await page.evaluate(() => ({
    bg: getComputedStyle(document.documentElement).getPropertyValue('--bg').trim(),
    accent: getComputedStyle(document.documentElement).getPropertyValue('--cyan').trim(),
  }))
  assert.equal(custom.bg, '#121212')
  assert.equal(custom.accent, '#ff00aa')
  assert(writes.some(w => w.body.includes('"id":"custom-demo-industrial"')), JSON.stringify(writes))

  // System theme follows the operating-system preference and reacts to it.
  await page.emulateMedia({ colorScheme: 'light' })
  await page.locator('button[aria-pressed]').filter({ hasText: 'Follow the system' }).first().click()
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'light')
  await page.emulateMedia({ colorScheme: 'dark' })
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'dark')

  // Layout and widgets: create a custom layout, disable Fans, reorder Camera.
  await page.getByRole('tab', { name: 'Dashboard layout', exact: true }).click()
  await page.getByRole('button', { name: 'Create a custom layout', exact: true }).click()
  await page.getByRole('tab', { name: 'Widgets', exact: true }).click()
  const fans = page.locator('[data-widget="fans"]')
  await fans.getByRole('switch', { name: 'Fans', exact: true }).click()
  await page.goto(origin + '#dashboard')
  await page.locator('.cc2-dashboard').waitFor()
  assert.equal(await page.locator('.cc2-dashboard-fans').count(), 0)
  const firstBefore = await page.locator('.cc2-widget-cell').first().getAttribute('data-widget')
  await page.goto(origin + '#appearance/widgets')
  const camera = page.locator('[data-widget="camera"]')
  await camera.getByRole('button', { name: /Move Camera down/ }).click()
  await page.goto(origin + '#dashboard')
  const firstAfter = await page.locator('.cc2-widget-cell').first().getAttribute('data-widget')
  assert.notEqual(firstBefore, firstAfter)

  // Profiles: save the current appearance and apply it again.
  await page.goto(origin + '#appearance/profiles')
  await page.getByRole('textbox', { name: 'Profile name' }).fill('Evening')
  const profileWrite = page.waitForResponse(r => r.url().endsWith('/api/appearance/profiles') && r.request().method() === 'POST')
  await page.getByRole('button', { name: 'Save current as profile', exact: true }).click()
  const profileResponse = await profileWrite
  assert.equal(profileResponse.status(), 200, await profileResponse.text())
  await page.getByText('Evening', { exact: true }).waitFor()
  await page.getByRole('button', { name: 'Apply', exact: true }).first().click()
  await page.waitForFunction(() => document.body.textContent.includes('Evening'))

  // The touchscreen tab is honest about this demo's missing manifest.
  await page.getByRole('tab', { name: 'Touchscreen', exact: true }).click()
  await page.getByText('Not supported', { exact: true }).waitFor()
  assert((await page.locator('.cc2-appearance').innerText()).toLowerCase().includes('manifest'))

  // Reload keeps the stored appearance (the mock backend is the source of truth).
  await page.reload()
  await page.getByRole('tab', { name: 'Touchscreen', exact: true }).waitFor()
  assert.equal(await page.evaluate(() => document.documentElement.dataset.theme), 'dark')

  // The studio must stay usable at phone width: wrapped tabs, no horizontal overflow.
  await page.setViewportSize({ width: 390, height: 900 })
  await page.goto(origin + '#appearance/themes')
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  await page.waitForTimeout(400)
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false)

  assert.deepEqual(errors, [])
  console.log('PASS: appearance themes, custom tokens, auto mode, layout/widget changes, profiles, touchscreen honesty, reload and phone width')
} finally {
  if (browser) await browser.close()
  await server.close()
}
