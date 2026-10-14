import assert from 'node:assert/strict'
import { copyFile, mkdir, readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions, root } from './fixture.mjs'

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
const labels = JSON.parse(await readFile(path.join(root, 'public/locales/it.json'), 'utf8'))
const output = process.env.CC2_PREVIEW_OUTPUT
try {
  const page = await browser.newPage({ viewport: { width: 1920, height: 1200 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(demo.url + '#control')
  await page.locator('.cc2-fans input').first().waitFor()
  await page.evaluate(() => document.fonts.ready)
  const geometry = () => page.locator('.cc2-control-columns > div > section').evaluateAll(cards =>
    cards.map(card => { const { x, y, width, height } = card.getBoundingClientRect(); return { x, y, width, height } }))
  if (process.argv.includes('--baseline')) {
    assert.ok(output)
    await mkdir(output, { recursive: true })
    await writeFile(path.join(output, 'control-before.json'), JSON.stringify(await geometry()))
  } else {
    const profiles = page.locator('.cc2-material-presets')
    const fields = profiles.locator('input')
    const save = profiles.getByRole('button', { name: labels['control.add_update'], exact: true })
    const select = page.locator('.cc2-preheat-presets')
    const nozzle = page.getByRole('spinbutton', { name: labels['control.name_target'].replace('{name}', labels['common.nozzle']) })
    const bed = page.getByRole('spinbutton', { name: labels['control.name_target'].replace('{name}', labels['common.heated_bed']) })
    assert.equal(await select.count(), 1)
    if (output) {
      const before = JSON.parse(await readFile(path.join(output, 'control-before.json'), 'utf8'))
      const after = await geometry()
      assert.equal(after.length, before.length)
      after.forEach((card, i) => Object.keys(card).forEach(k => assert.ok(Math.abs(card[k] - before[i][k]) <= 1, `card ${i} ${k} preserved`)))
    }
    const count = await select.locator('option').count()
    for (const name of ['PLA MATTE', 'PLA SILK']) {
      await fields.nth(0).fill(' ' + name + ' ')
      await fields.nth(1).fill('215')
      await fields.nth(2).fill('65')
      await save.click()
      await select.locator(`option[value="${name}"]`).waitFor({ state: 'attached' })
      await page.waitForFunction(name => document.querySelector('.cc2-material-presets input').value === name, name)
      await select.selectOption(name)
      assert.equal(await select.inputValue(), name, 'duplicate temperatures preserve chosen name')
    }
    assert.equal(await select.locator('option').count(), count + 2)
    await fields.nth(0).fill('PLA;M112')
    await save.click()
    assert.equal(await select.locator('option').count(), count + 2, 'unsafe names stay rejected')
    await page.reload()
    await select.locator('option[value="PLA MATTE"]').waitFor({ state: 'attached' })
    await select.selectOption('PLA MATTE')
    assert.equal(await nozzle.inputValue(), '215')
    assert.equal(await bed.inputValue(), '65')
    assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [], 'selection never starts heaters')
    await page.getByRole('button', { name: labels['control.apply_targets'], exact: true }).click()
    assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['heaters:set:215:65'])
    await nozzle.fill('216')
    assert.equal(await select.inputValue(), '', 'manual targets clear the preset match')
    for (const theme of ['dark', 'light', 'dracula', 'nord', 'monokai', 'solarized-light']) {
      await page.evaluate(theme => document.documentElement.dataset.theme = theme, theme)
      assert.equal(await select.evaluate(el => getComputedStyle(el).backgroundColor), await fields.nth(0).evaluate(el => getComputedStyle(el).backgroundColor), 'select follows field theme')
      for (const width of [1920, 1440, 768, 390]) {
        await page.setViewportSize({ width, height: 1200 })
        // Responsive sidebar widths transition after a resize; measure the settled layout.
        await page.waitForTimeout(350)
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `no overflow ${theme}/${width}`)
      }
    }
    await page.evaluate(() => { window.__CC2_PREVIEW.status = 2 })
    await page.waitForFunction(() => document.querySelector('.cc2-preheat-presets').disabled)
    await page.evaluate(() => { window.__CC2_PREVIEW.status = 1; window.__CC2_PREVIEW.connected = false })
    await page.waitForTimeout(1600)
    assert.ok(await select.isDisabled(), 'offline presets remain disabled')
    if (output) {
      await page.evaluate(() => { window.__CC2_PREVIEW.connected = true; document.documentElement.dataset.theme = 'dark' })
      await page.setViewportSize({ width: 1920, height: 1200 })
      await page.waitForFunction(() => !document.querySelector('.cc2-preheat-presets').disabled)
      await select.selectOption('PLA MATTE')
      await page.waitForTimeout(4500)
      await page.evaluate(() => scrollTo(0, 0))
      await page.waitForTimeout(300)
      await page.screenshot({ path: path.join(output, 'control-material-presets.png') })
      await copyFile(path.join(demo.folder, 'index.html'), path.join(output, 'index.html'))
    }
    assert.deepEqual(errors, [])
    console.log('PASS names with spaces, save/reload, duplicate targets, safe validation, no implicit heating, themed select, guards and unchanged card geometry')
  }
} finally {
  await browser.close()
  await demo.cleanup()
}
