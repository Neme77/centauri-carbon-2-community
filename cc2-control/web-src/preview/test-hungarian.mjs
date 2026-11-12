import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions, root } from './fixture.mjs'

// A queue for its page: a job fed from the Canvas with one copy printed and a timelapse, and one on the
// external feed, so the next job, its pre-flight check, the list and the policy are all shown.
const queueSetup = `(() => {
  const job = (id, path, mapping, filaments, extra) => Object.assign({ id, storage: 'internal', path, mapping, side: 'A',
    leveling: 'saved', timelapse: false, measure: '', nozzle: '', copies: 3, done: 1, estimated_seconds: 1500,
    filament_grams: 12.5, added: 1700000000, state: 'pending', last_result: '', present: true, filaments }, extra)
  const queue = { available: true, error: '', revision: 1, printer_idle: true, policy: { after: 'wait', delay: 60, remove_done: true },
    active: null, next: 1, auto_in: null, items: [
      job(1, 'part.gcode', '0:0', [{ tool: 0, type: 'PLA', name: 'PLA', color: '#FF0000', slot: 0, use: 0, reason: '' }], { timelapse: true }),
      job(2, 'feed.gcode', '', [], { copies: 1, done: 0 })] }
  const json = (data, status) => Promise.resolve(new Response(JSON.stringify(data), { status: status || 200, headers: { 'Content-Type': 'application/json' } }))
  const inner = window.fetch
  window.fetch = (input, options) => {
    const where = new URL(typeof input === 'string' ? input : input.url, location.href).pathname
    if (where.endsWith('/api/queue')) return json(queue)
    if (where.endsWith('/api/gcode-files/thumbnail')) return json({ error: 'No embedded thumbnail' }, 404)
    return inner(input, options)
  }
})()`
const demo = await fixture(queueSetup)
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(demo.url + '#settings')
  const language = page.locator('main select:has(option[value="hu"])')
  await page.getByRole('tab').nth(2).click()
  assert.deepEqual(await language.locator('option').allTextContents(),
    ['Čeština', 'Deutsch', 'English', 'Español', 'Français', 'Italiano', 'Magyar', 'Polski', 'Türkçe', 'Русский', '中文'],
    'language options follow displayed names, including accents, rather than language codes')
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
  // The queue page in Hungarian: none of its own strings is shown in English. A term that Hungarian keeps as
  // it is (the loanword Timelapse) is listed.
  const english = JSON.parse(await readFile(path.join(root, 'public/locales/en.json'), 'utf8'))
  const hungarian = JSON.parse(await readFile(path.join(root, 'public/locales/hu.json'), 'utf8'))
  const sameInHungarian = new Set(['queue.timelapse'])
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.evaluate(() => { location.hash = '#queue' })
  await page.getByRole('heading', { name: hungarian['queue.title'], exact: true }).waitFor()
  await page.getByText(hungarian['queue.next_job'], { exact: true }).first().waitFor()
  await page.getByText(hungarian['queue.preflight'], { exact: true }).first().waitFor()
  const shown = await page.locator('main').innerText()
  const left = Object.keys(english).filter(k => k.startsWith('queue.') && !sameInHungarian.has(k) && hungarian[k] === english[k] &&
    !english[k].includes('{') && english[k].length > 2 && shown.includes(english[k]))
  assert.deepEqual(left, [], 'queue strings shown in English on the Hungarian queue page')
  const context = await browser.newContext({ locale: 'hu-HU' })
  await context.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":""}'))
  const automatic = await context.newPage()
  await automatic.goto(demo.url + '#control')
  await automatic.waitForFunction(() => document.documentElement.lang === 'hu')
  await context.close()
  assert.deepEqual(errors, [])
  console.log('PASS: Hungarian selection, saved preference, regional detection, translated reset, responsive layouts, the queue page without English strings')
} finally {
  await browser.close()
  await demo.cleanup()
}
