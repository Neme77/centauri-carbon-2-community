import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import path from 'node:path'

import { fixture, launchOptions } from './fixture.mjs'
const demo = await fixture()
const base = demo.url
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1920, height: 1200 } })
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.goto(base + '#control')
  const commands = () => page.evaluate(() => window.__CC2_PREVIEW.commands)
  const numbers = page.locator('.cc2-fans input[type=number]')
  await numbers.nth(2).waitFor()
  await numbers.nth(0).fill('43')
  await page.evaluate(() => { window.__CC2_PREVIEW.fans.part = 20 })
  await page.waitForTimeout(1800)
  assert.equal(await numbers.nth(0).inputValue(), '43', 'polling must preserve typing')
  assert.equal((await commands()).length, 0, 'typing must not send commands')
  await numbers.nth(0).press('Enter')
  await page.waitForFunction(() => document.querySelector('.cc2-fans input[type=number]').value === '43' && !document.querySelector('.cc2-fans input[type=number]').disabled)
  assert.deepEqual(await commands(), ['fan:part:43'])
  await page.waitForTimeout(1700)
  for (const invalid of ['101', '', '37.5']) {
    await numbers.nth(0).fill(invalid)
    await numbers.nth(0).press('Enter')
  }
  assert.equal((await commands()).length, 1, 'invalid numbers must not reach the API')
  await numbers.nth(0).press('Escape')
  assert.equal(await numbers.nth(0).inputValue(), '43')
  await numbers.nth(1).fill('100')
  await numbers.nth(1).press('Enter')
  await page.waitForFunction(() => !document.querySelectorAll('.cc2-fans input[type=number]')[1].disabled)
  assert.equal((await commands()).at(-1), 'fan:aux:100')
  await numbers.nth(2).fill('0')
  await numbers.nth(2).press('Enter')
  await page.waitForFunction(() => !document.querySelectorAll('.cc2-fans input[type=number]')[2].disabled)
  assert.equal((await commands()).at(-1), 'fan:box:0')
  await page.evaluate(() => { window.__CC2_PREVIEW.reject = true })
  await numbers.nth(0).fill('75')
  await numbers.nth(0).press('Enter')
  await page.waitForFunction(() => document.querySelector('.cc2-fans input[type=number]').value === '43')
  await page.evaluate(() => { window.__CC2_PREVIEW.reject = false })
  const slider = page.locator('.cc2-fans input[type=range]').nth(2)
  await slider.focus()
  await slider.press('End')
  await page.waitForFunction(() => document.querySelectorAll('.cc2-fans input[type=number]')[2].value === '100')
  assert.equal((await commands()).at(-1), 'fan:box:100', 'slider keeps its existing commit path')
  await page.evaluate(() => { window.__CC2_PREVIEW.connected = false })
  await page.waitForFunction(() => [...document.querySelectorAll('.cc2-fans input')].every(input => input.disabled))
  await page.evaluate(() => { window.__CC2_PREVIEW.connected = true })
  await page.waitForFunction(() => [...document.querySelectorAll('.cc2-fans input')].every(input => !input.disabled))
  const card = selector => page.locator(selector).boundingBox()
  for (const width of [1920, 1440, 1024, 768, 390]) {
    await page.setViewportSize({ width, height: 1200 })
    await page.waitForTimeout(100)
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `no overflow at ${width}`)
    if (width >= 1440) {
      const fan = await card('.cc2-fans')
      const z = await card('.cc2-control-columns > div:last-child > section:last-child')
      assert.ok(Math.abs(fan.y - z.y) < 2 && Math.abs(fan.height - z.height) < 2, 'fan and Z cards align')
      const temp = await card('.cc2-control-columns > div:nth-child(2) > section:first-child')
      const machine = await card('.cc2-control-columns > div:last-child > section:first-child')
      assert.ok(Math.abs(temp.y - machine.y) < 2 && Math.abs(temp.height - machine.height) < 2, 'top cards align')
    }
    if (width === 1920 || width === 390)
      await page.screenshot({ path: path.join(demo.folder, `control-${width}.png`), fullPage: true })
  }
  assert.deepEqual(errors, [])
  console.log('PASS fan Enter/slider, range, Escape, telemetry focus, rejection/offline guards and desktop/mobile alignment')
} finally {
  await browser.close()
  await demo.cleanup()
}
