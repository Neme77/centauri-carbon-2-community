import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { mkdir } from 'node:fs/promises'
import path from 'node:path'
import { fixture, launchOptions } from './fixture.mjs'

const demo = await fixture(`
localStorage.setItem('cc2-language','en');
window.__CC2_PREVIEW.preferences.language='en';
const originalFetch=window.fetch;
window.__GRILLE_TEST={state:'standby',age:0,nozzle:0,bed:0,drying:false,hold:false};
window.fetch=async(input,options={})=>{
  const g=window.__GRILLE_TEST;
  if(String(input).endsWith('/api/control') && String(options.body).startsWith('grille:')){
    window.__CC2_PREVIEW.commands.push(String(options.body));
    if(g.hold) await new Promise(resolve=>window.__GRILLE_RELEASE=resolve);
    return new Response(JSON.stringify(window.__CC2_PREVIEW.reject?{error:'Native command refused'}:{accepted:true}),
      {status:window.__CC2_PREVIEW.reject?409:202,headers:{'Content-Type':'application/json'}});
  }
  const response=await originalFetch(input,options);
  if(String(input).endsWith('/api/printer')){
    const data=await response.json();
    data.print.native={state:g.state};data.last_message_age=g.age;
    data.extruder.target=g.nozzle;data.heater_bed.target=g.bed;data.bed_drying={active:g.drying};
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
  await page.goto(demo.url + '#control')
  const open = page.locator('.cc2-exhaust-grille button').nth(0)
  const close = page.locator('.cc2-exhaust-grille button').nth(1)
  const status = page.locator('.cc2-exhaust-grille [role=status]')
  await open.waitFor()
  await page.waitForFunction(() => !document.querySelector('.cc2-exhaust-grille button').disabled)
  assert.equal(await open.textContent(), 'Open flaps')
  await page.evaluate(() => { window.__GRILLE_TEST.hold = true })
  await open.click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.commands.length === 1)
  assert.equal(await close.isDisabled(), true)
  assert.match(await status.textContent(), /Sending/)
  await open.dispatchEvent('click')
  await close.dispatchEvent('click')
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['grille:open'])
  await page.evaluate(() => { window.__GRILLE_TEST.hold = false; window.__GRILLE_RELEASE() })
  await page.waitForFunction(() => document.querySelector('.cc2-exhaust-grille [role=status]').textContent.includes('Open command accepted'))
  await page.waitForTimeout(1800)
  assert.match(await status.textContent(), /Physical position is not verified/)
  await close.click()
  await page.waitForFunction(() => document.querySelector('.cc2-exhaust-grille [role=status]').textContent.includes('Close command accepted'))
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), ['grille:open','grille:close'])
  await page.evaluate(() => { window.__CC2_PREVIEW.reject = true })
  await open.click()
  await page.waitForFunction(() => document.querySelector('.cc2-exhaust-grille [role=status]').textContent.includes('Native command refused'))
  assert.doesNotMatch(await status.textContent(), /accepted/)
  for (const patch of [{state:'printing'},{state:'paused'},{state:''},{age:16},{age:-1},{nozzle:180},{bed:50},{drying:true}]) {
    await page.evaluate(p => Object.assign(window.__GRILLE_TEST,{state:'standby',age:0,nozzle:0,bed:0,drying:false},p),patch)
    await page.waitForTimeout(1700)
    assert.equal(await open.isDisabled(), true, JSON.stringify(patch))
  }
  await page.evaluate(() => { Object.assign(window.__GRILLE_TEST,{state:'complete',age:0,drying:false}); window.__CC2_PREVIEW.reject=false })
  await page.waitForFunction(() => !document.querySelector('.cc2-exhaust-grille button').disabled)
  await page.evaluate(() => { window.__CC2_PREVIEW.connected=false })
  await page.waitForFunction(() => document.querySelector('.cc2-exhaust-grille button').disabled)
  await page.evaluate(() => { window.__CC2_PREVIEW.connected=true })
  await page.waitForFunction(() => !document.querySelector('.cc2-exhaust-grille button').disabled)
  for(const width of [1440,768,390]){
    await page.setViewportSize({width,height:1000})
    await page.waitForTimeout(150)
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow ${width}`)
    assert.ok((await close.boundingBox()).width>100)
    if(process.env.CC2_GRILLE_SCREENSHOTS){
      await mkdir(process.env.CC2_GRILLE_SCREENSHOTS,{recursive:true})
      await page.screenshot({path:path.join(process.env.CC2_GRILLE_SCREENSHOTS,`control-${width}.png`),fullPage:true})
    }
  }
  assert.deepEqual(errors,[])
  console.log('PASS: actual grille buttons, native actions, pending double-click refusal, persistent accepted/error feedback, idle/heating/offline guards and responsive layout')
} finally { await browser.close(); await demo.cleanup() }
