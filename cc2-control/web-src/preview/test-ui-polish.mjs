import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo = await fixture(), browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  await page.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":"en"}'))
  await page.goto(demo.url + '#dashboard')
  const sidebar = page.locator('#cc2-navigation')
  const icon = sidebar.locator('a[href="#dashboard"] svg')
  const toggle = sidebar.locator('button[aria-expanded]')
  await toggle.waitFor()
  await page.waitForTimeout(400)
  const before = await icon.boundingBox(), buttonBefore = await toggle.boundingBox()
  await toggle.click(); await page.waitForTimeout(400)
  const after = await icon.boundingBox(), buttonAfter = await toggle.boundingBox()
  assert(Math.abs(before.x - after.x) < 1, 'sidebar icon must not jump horizontally')
  assert(Math.abs(buttonBefore.y - buttonAfter.y) < 1, 'collapse button must not jump vertically')
  for (const width of [1440, 1024, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))
  }
  await page.setViewportSize({ width: 1440, height: 1000 })
  await sidebar.locator('a[href="#files"]').click()
  await page.getByLabel('Sort by', { exact: true }).waitFor()
  assert(await page.locator('main').evaluate(el => el === document.activeElement))
  console.log('PASS: actual sidebar toggle preserves icon/button positions; desktop/mobile page bounds, navigation focus and accessible file sorting')
} finally { await browser.close(); await demo.cleanup() }
