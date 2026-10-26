import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  await page.addInitScript(() => {
    if (!localStorage.getItem('cc2-fixture-preferences'))
      localStorage.setItem('cc2-fixture-preferences', '{"language":"en","theme":"dark"}')
  })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  if (process.env.CC2_TEST_CASE !== 'camera') {
    await page.goto(demo.url + '#control')
    const range = page.locator('.cc2-fans input[type=range]').first()
    await range.waitFor()
    const box = await range.boundingBox()
    assert(box)
    await page.mouse.move(box.x + 8, box.y + box.height / 2)
    await page.mouse.down()
    await page.mouse.move(box.x + box.width * 0.8, box.y + box.height / 2, { steps: 12 })
    await page.evaluate(() => { window.__CC2_PREVIEW.fans.part = 15 })
    await page.waitForTimeout(1800)
    assert.equal(await range.isDisabled(), false, 'slider must remain enabled during a held mouse drag')
    assert.equal((await page.evaluate(() => window.__CC2_PREVIEW.commands)).length, 0, 'mouse drag must not commit before release')
    const draft = Number(await range.inputValue())
    assert(draft >= 65, 'telemetry must not reset the dragged thumb')
    await page.mouse.up()
    await page.waitForFunction(() => window.__CC2_PREVIEW.commands.length === 1)
    assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [`fan:part:${draft}`])
    console.log('PASS: held mouse drag survives telemetry and commits exactly once on release')
  }
  if (process.env.CC2_TEST_CASE !== 'fans') {
    for (const width of [1440, 1920, 768, 390]) {
      await page.setViewportSize({ width, height: 1000 })
      const dimensions = async route => {
        await page.goto(demo.url + '#' + route)
        const frame = page.locator('.cc2-camera-frame').first()
        await frame.waitFor()
        await page.waitForTimeout(400)
        return { frame: await frame.boundingBox(), card: await frame.locator('..').boundingBox() }
      }
      await page.goto(demo.url + '#dashboard')
      const flag = page.getByRole('checkbox', { name: 'Keep active', exact: true })
      await flag.uncheck()
      const before = {}
      for (const route of ['dashboard', 'job']) before[route] = await dimensions(route)
      await page.goto(demo.url + '#dashboard')
      await flag.check()
      for (const route of ['dashboard', 'job']) {
        const after = await dimensions(route)
        for (const element of ['frame', 'card']) for (const side of ['width', 'height']) {
          assert(Math.abs(before[route][element][side] - after[element][side]) <= 1,
            `${route}/${width} persistent ${element} ${side}: ${before[route][element][side]} -> ${after[element][side]}`)
        }
      }
    }
    console.log('PASS: persistent camera preserves normal card/frame dimensions on dashboard/job at four viewport widths')
  }
  assert.deepEqual(errors, [])
} finally {
  await browser.close()
  await demo.cleanup()
}
