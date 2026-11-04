import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions, root } from './fixture.mjs'

const demo = await fixture(`
window.__CC2_PREVIEW.preferences.language = 'en';
window.__CC2_PREVIEW.status = 2;
window.floor = {installed:true, ready:true, printing:true, minimum:null};
window.floorPosts = [];
window.floorReject = false;
const originalFetch = window.fetch;
window.fetch = async (input, options) => {
  if(String(input).endsWith('/api/cavity-fan-floor')) {
    if(options?.method === 'POST') {
      window.floorPosts.push(String(options.body));
      if(window.floorReject) return new Response(JSON.stringify({error:'Print state changed'}), {status:409, headers:{'Content-Type':'application/json'}});
      window.floor.minimum = Number(options.body) || null;
    }
    return new Response(JSON.stringify(window.floor), {headers:{'Content-Type':'application/json'}});
  }
  const response = await originalFetch(input, options);
  if(String(input).endsWith('/api/printer')) {
    const data = await response.json();
    data.machine.status_name = window.__CC2_PREVIEW.status === 2 ? 'Printing' : 'Idle';
    data.print = {enabled:window.__CC2_PREVIEW.status===2, state:window.__CC2_PREVIEW.status===2?'printing':'standby', filename:'fixture.gcode'};
    return new Response(JSON.stringify(data), {headers:{'Content-Type':'application/json'}});
  }
  return response;
};`)
const labels = JSON.parse(await readFile(path.join(root, 'public/locales/en.json'), 'utf8'))
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({viewport:{width:390,height:844}})
  const errors = []
  page.on('pageerror', error => errors.push(error.message))
  await page.goto(demo.url + '#control')
  const block = page.locator('.cc2-cavity-floor')
  const field = block.getByRole('spinbutton', {name:labels['fanfloor.minimum_label']})
  const apply = block.getByRole('button', {name:labels['fanfloor.apply'],exact:true})
  const automatic = block.getByRole('button', {name:labels['fanfloor.automatic'],exact:true})
  await field.waitFor()
  await field.fill('75')
  // A normal status poll must not overwrite an edit or issue a command.
  await page.waitForTimeout(5200)
  assert.equal(await field.inputValue(), '75')
  assert.deepEqual(await page.evaluate(() => window.floorPosts), [])
  await apply.click()
  await page.waitForFunction(() => window.floor.minimum === 75)
  await page.waitForFunction(label => document.querySelector('.cc2-cavity-floor [role="status"]').textContent === label, labels['fanfloor.accepted'])
  await automatic.click()
  await page.waitForFunction(() => window.floor.minimum === null)
  assert.deepEqual(await page.evaluate(() => window.floorPosts), ['75','0'])
  await field.fill('101');await apply.click()
  assert.deepEqual(await page.evaluate(() => window.floorPosts), ['75','0'])
  await page.evaluate(() => {window.floorReject=true})
  await field.fill('60');await apply.click()
  await block.getByText('Print state changed',{exact:true}).waitFor()
  await page.evaluate(() => {window.floor.printing=false;window.__CC2_PREVIEW.status=1})
  await page.waitForFunction(label => document.querySelector('input[aria-label="'+label+'"]').disabled, labels['fanfloor.minimum_label'])
  assert.ok(await apply.isDisabled())
  for(const width of [390,1440]){
    await page.setViewportSize({width,height:900});await page.waitForTimeout(250)
    assert.ok(await block.evaluate(element => element.getBoundingClientRect().right <= innerWidth + 1))
  }
  await page.evaluate(() => {window.floor.installed=false})
  await block.waitFor({state:'detached'})
  assert.deepEqual(errors,[])
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [], 'No G-code or periodic fan command')
  console.log('PASS: actual built exhaust controls, deliberate minimum/Automatic, retained edits, native refusal, print end, absent addon, desktop/mobile layout and no G-code polling.')
} finally {
  await browser.close();await demo.cleanup()
}
