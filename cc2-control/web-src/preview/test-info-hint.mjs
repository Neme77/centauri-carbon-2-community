// The small (i) of the Bed alignment tab (components/info-hint.tsx): it opens on hover, on click or tap (a touch screen
// has no hover) and from the keyboard, and closes on Escape, on a click elsewhere or when the pointer leaves. Also the
// first start of step 1: only a temperature (no plate) is chosen, the start row is one row.
import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' })
let browser
try {
  await server.listen()
  browser = await chromium.launch({
    headless: true,
    ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : process.env.CC2_BROWSER_EXECUTABLE ? { executablePath: process.env.CC2_BROWSER_EXECUTABLE } : {}),
  })
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  const errors = []
  const open = async context => {
    const page = await context.newPage()
    page.on('pageerror', e => errors.push(e.message))
    await page.goto(origin)
    await page.locator('#cc2-preview-banner').waitFor()
    await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
    await page.locator('#cc2-preview-banner').waitFor()
    await page.goto(`${origin}/#bed/align`)
    await page.locator('[data-level-probe-twice]').waitFor({ timeout: 15000 })
    return page
  }
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } })
  const page = await open(context)
  const button = page.locator('[data-hint="probe-twice"] button')
  const text = page.locator('[data-hint-text]')
  const sentence = 'Double measurement, the value is the average.'

  // The first start: a temperature, no plate; the ways of measuring, the tick and the probing stand on one row above
  // the start.
  assert.equal(await page.locator('[data-level-base]').evaluate(e => e.value), 'material:PLA', 'first start: only a temperature, no plate')
  assert.equal(await page.locator('[data-level-base]').evaluate(e => e.options[e.selectedIndex].text), 'PLA · 60 °C')
  const mode = await page.locator('[data-level-start-mode]').boundingBox()
  const twice = await page.locator('[data-level-probe-twice]').boundingBox()
  const start = await page.locator('[data-screw-entries]').boundingBox()
  assert.ok(twice.x >= mode.x + mode.width && twice.y < mode.y + mode.height, 'the probing choice is right of the ways, on their row')
  assert.ok(start.y >= mode.y + mode.height, 'the start button is below them')

  // The tabs of Bed levelling: mesh, bed alignment, build plates and nozzles, nozzle expansion (the load cell lives in the mesh tab).
  const tabs = await page.locator('[role="tab"]').allInnerTexts()
  assert.equal(tabs.length, 4)
  assert.deepEqual(
    await page.locator('[role="tab"]').evaluateAll(es => es.map(e => e.getAttribute('aria-selected'))),
    ['false', 'true', 'false', 'false'],
    'the second tab, Bed alignment, is the open one for #bed/align'
  )
  assert.match(tabs[0], /Mesh/)
  assert.match(tabs[1], /Bed alignment/)
  assert.match(tabs[2], /Build plates & nozzles/)
  assert.match(tabs[3], /Nozzle expansion/)

  // Hover opens and leaving closes.
  assert.equal(await text.count(), 0, 'closed at first')
  await button.hover()
  await text.waitFor()
  assert.equal(await text.innerText(), sentence)
  await page.mouse.move(5, 5)
  await text.waitFor({ state: 'detached' })
  // A click keeps it open after the pointer left; Escape closes it.
  await button.click()
  await page.mouse.move(5, 5)
  await text.waitFor()
  assert.equal(await button.getAttribute('aria-expanded'), 'true')
  await page.keyboard.press('Escape')
  await text.waitFor({ state: 'detached' })
  // The keyboard reaches it and opens it; a click elsewhere closes it.
  await button.focus()
  await page.keyboard.press('Enter')
  await text.waitFor()
  await page.mouse.click(1420, 980)
  await text.waitFor({ state: 'detached' })
  // The explanation is also the accessible name.
  assert.equal(await button.getAttribute('aria-label'), sentence)
  await context.close()

  // Touch: a tap opens it (a touch screen has no hover).
  const touch = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true })
  const phone = await open(touch)
  await phone.locator('[data-hint="probe-twice"] button').tap()
  await phone.locator('[data-hint-text]').waitFor()
  assert.equal(await phone.locator('[data-hint-text]').innerText(), sentence)
  assert.ok(await phone.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no sideways scrolling with the hint open')
  await touch.close()

  assert.deepEqual(errors, [])
  console.log('PASS info hint: hover, click, Escape, keyboard, click elsewhere, tap; first start with a temperature only')
} finally {
  await browser?.close()
  await server.close()
}
