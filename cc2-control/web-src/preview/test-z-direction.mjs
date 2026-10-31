import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, machine = 1, homed = 'xyz'
const actions = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', r => r.fulfill({ json: { connected: true, last_message_age: 0, machine: { status: machine, status_name: machine === 1 ? 'Idle' : 'Printing' }, motion: { homed_axes: homed, x: 100, y: 100, z: 50 }, print: { state: machine === 1 ? 'idle' : 'printing' }, extruder: { temperature: 25, target: 0 }, heater_bed: { temperature: 25, target: 0 } } }))
  await page.route('**/api/pid', r => r.fulfill({ json: { busy: false, ready: false } }))
  await page.route('**/api/console', r => r.fulfill({ json: { busy: false, completed: false, success: false, command: '', output: '' } }))
  await page.route('**/api/control', r => { actions.push(r.request().postData()); return r.fulfill({ status: 202, json: { accepted: true } }) })
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  const movement = page.locator('.cc2-movement')
  const z = movement.locator('.cc2-motion-pad > div').last().getByRole('button')
  await movement.waitFor()
  assert.equal(await z.count(), 2)
  const up = z.nth(0), down = z.nth(1)
  await page.waitForFunction(() => !document.querySelector('.cc2-motion-pad > div:last-child button')?.disabled)
  await mkdir('preview/artifacts', { recursive: true })
  for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(200)
    for (const step of [0.1, 1, 10]) {
      await movement.getByRole('button', { name: `${step} mm`, exact: true }).click()
      const before = actions.length
      const raised = page.waitForResponse(r => r.url().endsWith('/api/control') && r.request().method() === 'POST')
      await up.click(); await raised
      const lowered = page.waitForResponse(r => r.url().endsWith('/api/control') && r.request().method() === 'POST')
      await down.click(); await lowered
      assert.deepEqual(actions.slice(before), [`move:Z:${-step}`, `move:Z:${step}`], `Z direction regression at ${width}px / ${step}mm`)
    }
    assert.equal(await up.getAttribute('aria-label'), 'Move Z negative')
    assert.equal(await down.getAttribute('aria-label'), 'Move Z positive')
    assert.equal((await up.textContent()).trim(), '', 'keep the upper control icon-only')
    assert.equal((await down.textContent()).trim(), '', 'keep the lower control icon-only')
    assert.match(await up.locator('svg').getAttribute('class'), /arrow-big-up/)
    assert.match(await down.locator('svg').getAttribute('class'), /arrow-big-down/)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'no horizontal overflow')
    await movement.screenshot({ path: `preview/artifacts/z-direction-${width}.png` })
  }
  const before = actions.length
  homed = 'xy'
  await page.waitForFunction(() => document.querySelector('.cc2-motion-pad > div:last-child button')?.disabled)
  assert(await up.isDisabled()); assert(await down.isDisabled())
  await up.evaluate(b => b.click()); await down.evaluate(b => b.click())
  assert.equal(actions.length, before, 'unhomed Z must not send moves')
  homed = 'xyz'; machine = 2
  await page.waitForFunction(() => document.querySelector('main').textContent.includes('Printing'))
  assert(await up.isDisabled()); assert(await down.isDisabled())
  await up.evaluate(b => b.click()); await down.evaluate(b => b.click())
  assert.equal(actions.length, before, 'printing must not send manual Z moves')
  assert.deepEqual(errors, [])
  console.log('PASS actual upper/lower Z buttons, signed actions at 0.1/1/10 mm, accessible names, arrows only, desktop/mobile and homing/printing guards')
} finally { await browser?.close(); await server.close() }
