import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
// An accidental proxy would fail the API and browser checks below.
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } })
  const errors = [], outside = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (!r.url().startsWith(`${origin}/`) && !r.url().startsWith('blob:') && !r.url().startsWith('data:')) outside.push(r.url())
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await page.waitForFunction(() => document.querySelector('main')?.textContent.includes('143'))
  assert.equal((await page.request.get(`${origin}/api/printer`)).status(), 200)
  assert.equal((await page.request.get(`${origin}/api/unknown`)).status(), 404)
  assert.equal((await page.request.post(`${origin}/api/control`, { data: 'system:emergency_stop' })).status(), 403)
  assert.equal((await page.request.post(`${origin}/api/gcode-files/upload?name=demo.gcode`, { data: 'G28' })).status(), 403)
  assert.equal((await page.request.post(`${origin}/api/console/command`, { data: 'G28' })).status(), 403)
  assert.equal((await page.request.post(`${origin}/api/setup`, { data: '123456' })).status(), 403)
  const alignedControl = async () => {
    const rows = await page.locator('.cc2-control-columns').evaluate(e => {
      const groups = [...e.children].map(g => [...g.children].map(c => c.getBoundingClientRect()))
      return [Math.abs(groups[1][0].top - groups[2][0].top), Math.abs(groups[1][0].bottom - groups[2][0].bottom), Math.abs(groups[1][1].top - groups[2][1].top), Math.abs(groups[1][1].bottom - groups[2][1].bottom)]
    })
    assert.ok(rows.every(d => d < 2), 'Control cards must share desktop row edges')
  }
  const shots = process.env.CC2_SCREENSHOTS
  if (shots) await mkdir(shots, { recursive: true })
  for (const [width, height] of [[1440, 900], [1366, 640], [1280, 720], [1920, 1080], [1024, 768], [768, 1024], [390, 844], [740, 390], [1280, 480]]) {
    await page.setViewportSize({ width, height })
    const rail = page.locator('#cc2-navigation button[aria-expanded]')
    if (await rail.isVisible() && await rail.getAttribute('aria-expanded') !== 'true') { await rail.click(); await page.waitForTimeout(200) }
    for (const tab of ['dashboard', 'control', 'job', 'files', 'bed', 'canvas', 'console', 'settings']) {
      if (await page.locator('#cc2-menu-toggle').isVisible()) await page.locator('#cc2-menu-toggle').click()
      await page.locator(`a[href="#${tab}"]`).click()
      await page.waitForTimeout(150)
      assert.ok(await page.locator('main').innerText(), `${width} ${tab}: empty page`)
      const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1)
      assert.equal(overflow, false, `${width} ${tab}: horizontal overflow`)
      if (tab === 'control' && width >= 1280 && height > 500) await alignedControl()
      if (shots) await page.screenshot({ path: path.join(shots, `${width}-${tab}.png`), fullPage: true })
    }
  }
  for (const [width, height] of [[1366, 640], [1280, 720], [1920, 1080], [1024, 768]]) {
    await page.setViewportSize({ width, height })
    const rail = page.locator('#cc2-navigation button[aria-expanded]')
    if (await rail.getAttribute('aria-expanded') !== 'true') await rail.click()
    await page.waitForTimeout(200)
    await rail.click()
    await page.waitForTimeout(200)
    assert.equal(await page.locator('header').evaluate(e => Math.round(e.getBoundingClientRect().height)), 52)
    for (const tab of ['dashboard', 'control', 'job', 'files', 'bed', 'canvas', 'console', 'settings']) {
      await page.locator(`a[href="#${tab}"]`).click()
      await page.waitForTimeout(150)
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, `${width} collapsed ${tab}: overflow`)
      if (tab === 'control' && width >= 1280) await alignedControl()
      if (tab === 'dashboard') assert.ok(await page.locator('.cc2-camera-frame').evaluate(e => e.getBoundingClientRect().height <= 241))
      if (shots) await page.screenshot({ path: path.join(shots, `${width}-collapsed-${tab}.png`), fullPage: true })
    }
  }
  await page.setViewportSize({ width: 1440, height: 900 })
  const rail = page.locator('#cc2-navigation button[aria-expanded]')
  if (await rail.getAttribute('aria-expanded') !== 'true') await rail.click()
  await page.waitForTimeout(200)
  await page.locator('#cc2-navigation button[aria-expanded]').click()
  for (const tab of ['dashboard', 'control', 'job', 'files', 'bed', 'canvas', 'console', 'settings']) {
    await page.locator(`a[href="#${tab}"]`).click()
    await page.waitForTimeout(200)
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, `Collapsed sidebar: ${tab} overflow`)
    if (shots) await page.screenshot({ path: path.join(shots, `1440-collapsed-${tab}.png`), fullPage: true })
  }
  await page.getByRole('tab').nth(2).click()
  const languages = page.locator('main select:has(option[value="zh"])')
  for (const lang of ['en', 'fr', 'zh', 'it']) {
    await languages.selectOption(lang)
    await page.waitForFunction(lang => document.documentElement.lang === lang, lang)
  }
  const themes = page.locator('main select:has(option[value="light"])')
  for (const theme of ['light', 'dark']) {
    await themes.selectOption(theme)
    await page.waitForFunction(theme => document.documentElement.dataset.theme === theme, theme)
  }
  await page.locator('a[href="#dashboard"]').click()
  // Tuning reset exercises the same form action as the real UI, but only modifies fixtures.
  const response = page.waitForResponse(r => r.url().endsWith('/api/control') && r.request().method() === 'POST')
  await page.locator('#tune-speed').locator('..').getByRole('button').last().click()
  assert.equal((await response).status(), 200)
  for (const [scenario, expected] of [['paused', 'paused'], ['idle', ''], ['disconnected', ''], ['printing', 'printing']]) {
    await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption(scenario)])
    await page.locator('#cc2-preview-banner').waitFor()
    const data = await (await page.request.get(`${origin}/api/printer`)).json()
    assert.equal(data.print.state, expected)
    assert.equal(data.connected, scenario !== 'disconnected')
  }
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-reset').click()])
  assert.equal((await (await page.request.get(`${origin}/__preview/scenario`)).json()).scene, 'printing')
  assert.deepEqual(errors, [])
  assert.deepEqual(outside, [], 'Preview must never request printer ports or external services')
  console.log('PASS: isolated preview, all pages at 9 viewport sizes and desktop sidebar states, scenario controls, tuning reset, blocked hardware actions and local-only requests')
} finally {
  await browser?.close()
  await server.close()
}
