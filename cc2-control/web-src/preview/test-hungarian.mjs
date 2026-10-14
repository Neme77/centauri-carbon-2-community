import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions, root } from './fixture.mjs'

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(demo.url + '#settings')
  const language = page.locator('main select:has(option[value="hu"])')
  for (const [code, name] of [['hu', 'Magyar']]) {
    await page.getByRole('tab').nth(2).click()
    assert.equal(await language.locator(`option[value="${code}"]`).textContent(), name)
    const labels = JSON.parse(await readFile(path.join(root, `public/locales/${code}.json`), 'utf8'))
    await language.selectOption(code)
    await page.waitForFunction(c => document.documentElement.lang === c, code)
    assert.equal(await page.evaluate(() => window.__CC2_PREVIEW.preferences.language), code)
    await page.reload()
    await page.waitForFunction(c => document.documentElement.lang === c, code)
    for (const tab of ['control', 'canvas', 'settings']) {
      await page.evaluate(t => { location.hash = '#' + t }, tab)
      await page.waitForTimeout(300)
      if (tab === 'control') {
        assert.equal(await page.getByRole('heading', { name: labels['common.control'], exact: true }).count(), 1)
        assert.equal(await page.locator('.cc2-fans input[type=number]').count(), 3)
      }
      if (tab === 'canvas') {
        const reset = page.getByRole('button', { name: labels['canvas.reset_all'], exact: true })
        await reset.click()
        await page.getByText(labels['canvas.reset_all_confirm'], { exact: true }).waitFor()
        await page.getByRole('button', { name: labels['common.cancel'], exact: true }).click()
        assert.equal((await page.evaluate(() => window.__CC2_PREVIEW.commands)).length, 0)
      }
      for (const width of [1440, 768, 390]) {
        await page.setViewportSize({ width, height: 1000 })
        await page.waitForTimeout(100)
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${code}/${tab}/${width} overflow`)
      }
    }
  }
  const context = await browser.newContext({ locale: 'hu-HU' })
  await context.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":""}'))
  const automatic = await context.newPage()
  await automatic.goto(demo.url + '#control')
  await automatic.waitForFunction(() => document.documentElement.lang === 'hu')
  await context.close()
  assert.deepEqual(errors, [])
  console.log('PASS: Hungarian selection, saved preference, regional detection, translated reset, responsive layouts')
} finally {
  await browser.close()
  await demo.cleanup()
}
