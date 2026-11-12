import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
import { settledViewport } from './viewport.mjs'

const demo = await fixture(`
localStorage.setItem('cc2-language','en');
const f=window.__CC2_PREVIEW;
f.preferences.language='en';
f.preferences.quick_actions ||= ['grille:open','grille:close','control:tuning','settings:connection'];
window.__QUICK={native:'standby',age:0,target:0,heating:false,hold:false,enabled:true};
const original=window.fetch;
window.fetch=async(input,options={})=>{
 const q=window.__QUICK,path=String(input);
 if(path.endsWith('/api/control') && String(options.body).startsWith('grille:')){
  f.commands.push(String(options.body));
  if(q.hold)await new Promise(resolve=>window.__QUICK_RELEASE=resolve);
  return new Response(JSON.stringify(f.reject?{error:'Native grille refused'}:{accepted:true}),
   {status:f.reject?409:202,headers:{'Content-Type':'application/json'}});
 }
 const response=await original(input,options);
 if(path.endsWith('/api/preferences') && options.method==='PUT'){
  const body=JSON.parse(options.body);
  f.preferences.quick_actions=[1,2,3,4].map(i=>body['quick'+i]);
  localStorage.setItem('cc2-fixture-preferences',JSON.stringify(f.preferences));
 }
 if(path.endsWith('/api/printer')){
  const data=await response.json();
  data.print.native={state:q.native};data.last_message_age=q.age;
  data.extruder.target=q.target;data.bed_drying={enabled:q.enabled,active:q.heating,state:'off'};
  data.tuning.pressure_advance=0.04;
  return new Response(JSON.stringify(data),{headers:{'Content-Type':'application/json'}});
 }
 return response;
};
`)
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(demo.url + "#dashboard")
  const quick = page.locator('.cc2-quick-actions')
  const open = quick.getByRole('button', { name: 'Open flaps', exact: true })
  const close = quick.getByRole('button', { name: 'Close flaps', exact: true })
  await open.waitFor()
  await page.waitForFunction(() => !document.querySelector('.cc2-quick-grid button').disabled)
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [], 'page open sends no hardware command')
  await page.evaluate(() => { window.__QUICK.hold = true })
  await open.click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.commands.length === 1)
  assert(await close.isDisabled(), 'all flap shortcuts share the pending lock')
  await close.dispatchEvent('click')
  await page.locator('#cc2-navigation a[href="#control"]').click()
  assert(await page.locator('.cc2-exhaust-grille button').first().isDisabled(), 'fan panel shares the lock after navigation')
  await page.evaluate(() => { window.__QUICK.hold = false; window.__QUICK_RELEASE() })
  await page.waitForFunction(() => !document.querySelector('.cc2-exhaust-grille button').disabled)
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  await close.click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.commands.length === 2)
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['grille:open', 'grille:close'])
  for (const patch of [{native:'printing'}, {native:'paused'}, {age:16}, {age:-1}, {target:180}, {heating:true}]) {
    await page.evaluate(p => Object.assign(window.__QUICK, {native:'standby',age:0,target:0,heating:false}, p), patch)
    await page.waitForTimeout(1700)
    await page.waitForFunction(() => document.querySelector('.cc2-quick-grid button').disabled)
    await open.dispatchEvent('click')
    assert.equal((await page.evaluate(() => window.__CC2_PREVIEW.commands)).length, 2)
  }
  await page.evaluate(() => Object.assign(window.__QUICK, {native:'complete',age:0,target:0,heating:false}))
  await page.waitForFunction(() => !document.querySelector('.cc2-quick-grid button').disabled)
  await page.evaluate(() => { window.__CC2_PREVIEW.reject = true })
  await open.click()
  await page.getByText(/Native grille refused/).waitFor()
  await page.evaluate(() => { window.__CC2_PREVIEW.reject = false })
  const commandCount = await page.evaluate(() => window.__CC2_PREVIEW.commands.length)
  await quick.getByRole('button', { name: 'Pressure Advance (s)', exact: true }).click()
  await page.waitForFunction(() => document.activeElement?.id === 'tune-pressure-advance')
  assert.equal(await page.locator('#tune-pressure-advance').getAttribute('tabindex'), null)
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  await quick.getByRole('button', { name: 'Connections', exact: true }).click()
  await page.waitForURL('**#settings/connection')
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  await quick.getByRole('button', { name: 'Edit', exact: true }).click()
  const dialog = page.getByRole('dialog')
  for (const id of ['#quick-slot-0', '#quick-slot-1']) {
    assert.equal(await page.locator(id).locator('option[value^="cell:"]').count(), 0)
    assert.equal(await page.locator(id).locator('option[value^="control:"]').count(), 3)
  }
  await page.locator('#quick-slot-0').selectOption('control:fans')
  await page.locator('#quick-slot-1').selectOption('control:heating')
  await dialog.getByRole('button', { name: 'Save Changes', exact: true }).click()
  await dialog.waitFor({ state: 'hidden' })
  await page.reload()
  const fans = quick.getByRole('button', { name: 'Fans', exact: true })
  await fans.waitFor()
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.preferences.quick_actions), ['control:fans','control:heating','control:tuning','settings:connection'])
  await fans.click()
  await page.waitForFunction(() => document.activeElement?.classList.contains('cc2-fans'))
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  await quick.getByRole('button', { name: 'Bed heating and chamber preheat', exact: true }).click()
  await page.waitForFunction(() => document.activeElement?.hasAttribute('data-bed-drying'))
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  await page.evaluate(() => { window.__QUICK.enabled = false })
  await page.waitForTimeout(1700)
  await quick.getByRole('button', { name: 'Bed heating and chamber preheat', exact: true }).click()
  await page.waitForURL('**#settings/integrations')
  // Navigation-only shortcuts never dispatch a control/heating/calibration request.
  assert.equal(await page.evaluate(() => window.__CC2_PREVIEW.commands.length), 0, 'reload resets fixture command history; all later shortcuts only navigate')
  assert.equal(commandCount, 3)
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  for (const width of [1440, 768, 390]) {
    const geometry = await settledViewport(page, width, 1000)
    assert.equal(await quick.locator('.cc2-quick-grid > button').count(), 4)
    assert(geometry.width <= geometry.viewport, `overflow at ${width}: ${JSON.stringify(geometry)}`)
  }
  assert.deepEqual(errors, [])
  console.log('PASS: persisted quick choices, shared flap guards/lock and feedback, navigation-only controls, experimental heating fallback and responsive four-slot layout; no cell shortcut')
} finally {
  await browser.close()
  await demo.cleanup()
}
