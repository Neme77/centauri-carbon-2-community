import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions, root } from './fixture.mjs'

const demo = await fixture(`
window.__CC2_PREVIEW.preferences.language = 'en';
window.__CC2_PREVIEW.status = 2;
const originalFetch = window.fetch;
window.fetch = async (input, options) => {
  const response = await originalFetch(input, options);
  if (String(input).endsWith('/api/printer')) {
    const data = await response.json();
    data.extruder.target = 320; data.heater_bed.target = 60;
    const status = window.__CC2_PREVIEW.status;
    data.machine.status_name = status === 2 ? 'Printing' : status === 1 ? 'Idle' : 'Loading';
    data.print = {enabled: status === 2, filename: status === 2 ? 'fixture.gcode' : '', state: status === 2 ? 'printing' : status === 1 ? 'standby' : 'loading'};
    return new Response(JSON.stringify(data), {headers: {'Content-Type': 'application/json'}});
  }
  return response;
};`)
const labels = JSON.parse(await readFile(path.join(root, 'public/locales/en.json'), 'utf8'))
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage()
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.goto(demo.url + '#control')
  const targetLabel = labels['control.name_target'].replace('{name}', labels['common.nozzle'])
  const nozzle = page.getByRole('spinbutton', {name: targetLabel, exact: true})
  const apply = page.getByRole('button', {name: labels['control.apply_targets'], exact: true})
  await nozzle.waitFor()
  await page.waitForFunction(label => document.querySelector(`input[aria-label="${label}"]`)?.value === '320', targetLabel)
  assert.equal(await nozzle.getAttribute('max'), '350')
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [], 'Reading a 320 C target must not send any command')
  for (const target of ['301', '320', '350']) {
    await nozzle.fill(target)
    await apply.click()
    await page.waitForFunction(target => window.__CC2_PREVIEW.commands.at(-1) === `heaters:set:${target}:60`, target)
  }
  for (const target of ['351', '-1']) {
    await nozzle.fill(target)
    await apply.click()
  }
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['heaters:set:301:60', 'heaters:set:320:60', 'heaters:set:350:60'])
  await page.evaluate(() => { window.__CC2_PREVIEW.status = 3 })
  await page.waitForFunction(label => document.querySelector(`input[aria-label="${label}"]`)?.disabled, targetLabel)
  assert.ok(await apply.isDisabled())
  await page.evaluate(() => { window.__CC2_PREVIEW.status = 1 })
  const select = page.locator('.cc2-preheat-presets')
  await page.waitForFunction(() => !document.querySelector('.cc2-preheat-presets').disabled)
  const profiles = page.locator('.cc2-material-presets')
  const fields = profiles.locator('input')
  await fields.nth(0).fill('PPS-CF10')
  await fields.nth(1).fill('350')
  await fields.nth(2).fill('100')
  await profiles.getByRole('button', {name: labels['control.add_update'], exact: true}).click()
  await select.locator('option[value="PPS-CF10"]').waitFor({state: 'attached'})
  await select.selectOption('PPS-CF10')
  assert.equal(await nozzle.inputValue(), '350')
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['heaters:set:301:60', 'heaters:set:320:60', 'heaters:set:350:60'], 'Saving/selecting a 350 C profile must not start heaters')
  assert.deepEqual(errors, [])
  console.log('PASS: actual built UI accepts 301/320/350 C during printing, retains a native 320 C target, refuses out-of-range values and locks during other operations; no implicit heating.')
} finally {
  await browser.close()
  await demo.cleanup()
}
