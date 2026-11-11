import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture(`
window.__CC2_PREVIEW.preferences.language='en';
const original=window.fetch;
window.__TOOLS={busy:false,generation:0,error:'none',mode:0,active:null,original:null,configured:28.8,saved:null,editable:true,pending_restart:false,pending_conflict:false,reports:[],cell:{stage:0,owned:false,max_grams:1000,counts_per_gram:0,tare_counts:null,grams:99.99}};
window.__ACTIONS=[];window.__GETS=0;
window.fetch=async(input,options={})=>{
 const path=String(input),reply=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'Content-Type':'application/json'}});
 if(path.endsWith('/api/calibration-tools/status')){window.__GETS++;return reply(window.__TOOLS)}
 if(path.endsWith('/api/calibration-tools/action')){window.__ACTIONS.push(String(options.body));Object.assign(window.__TOOLS,{busy:true,mode:6,generation:window.__TOOLS.generation+1});return reply({accepted:true},202)}
 const response=await original(input,options);
 if(path.endsWith('/api/printer')){const data=await response.json();data.print.native={state:window.__CC2_PREVIEW.status===2?'printing':'standby'};data.extruder={temperature:25,target:0};data.heater_bed={temperature:22,target:0};return reply(data)}
 return response;
};
`)
const browser=await chromium.launch(launchOptions)
try{
 const page=await browser.newPage({viewport:{width:1440,height:1200}}),errors=[]
 page.on('pageerror',e=>errors.push(e.message))
 await page.goto(demo.url+'#bed/cell')
 const card=page.locator('.cc2-native-tools'),guide=card.locator('.cc2-cell-guide')
 const button=name=>card.getByRole('button',{name,exact:true})
 const actions=()=>page.evaluate(()=>window.__ACTIONS)
 await button('Guided calibration').waitFor()
 await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Guided calibration').disabled)
 assert.equal(await guide.count(),0)
 assert.equal(await button('Read active distance').count(),0,'rotation controls belong to Print tuning')
 assert.equal((await actions()).length,0)
 await button('Guided calibration').click();await page.getByRole('button',{name:'Cancel',exact:true}).click()
 assert.equal((await actions()).length,0)
 await button('Tare').click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>window.__ACTIONS.length===1)
 assert.equal((await actions()).at(-1),'cell:tare')
 async function finish(stage){
  await page.evaluate(stage=>{window.__TOOLS.busy=false;window.__TOOLS.cell.stage=stage;window.__TOOLS.cell.owned=!!stage;window.__TOOLS.cell.counts_per_gram=stage>=3?25.125:0;window.__TOOLS.cell.tare_counts=stage>=4?-125:null},stage)
  await page.waitForFunction(stage=>!document.querySelector('.cc2-native-tools').textContent.includes('Native tool running')&& (!!document.querySelector('.cc2-cell-guide'))===(stage!==0),stage)
 }
 async function click(name,confirm=false){
  const before=(await actions()).length
  const local=guide.getByRole('button',{name,exact:true})
  await (await local.count()?local:button(name)).click()
  if(confirm)await page.getByRole('button',{name:'Confirm',exact:true}).click()
  await page.waitForFunction(before=>window.__ACTIONS.length>before,before)
 }
 await finish(0)
 assert.equal(await page.getByRole('tab',{name:'Load cell',exact:true}).getAttribute('aria-selected'),'true')
 await click('Guided calibration',true);assert.equal((await actions()).at(-1),'cell:start');await finish(1)
 assert.equal(await button('Apply calibration').count(),0,'no acceptance without tare and measurement')
 assert.equal(await button('Read load cell').isDisabled(),true)
 await click('Tare',true);assert.equal((await actions()).at(-1),'cell:zero');await finish(2)
 const mass=guide.getByRole('spinbutton')
 assert.equal(await mass.inputValue(),'','a new session must not supply an assumed 500 g load')
 assert.equal(await button('Measure').isDisabled(),true)
 await mass.fill('1200');assert.equal(await button('Measure').isDisabled(),true)
 await mass.fill('99.99')
 await button('Measure').click()
 await page.getByRole('dialog').getByText(/99\.99/).waitFor()
 await page.getByRole('button',{name:'Cancel',exact:true}).click()
 assert.ok(!(await actions()).some(a=>a.startsWith('cell:measure:')),'cancel must not measure with the wrong mass')
 await click('Measure',true);assert.equal((await actions()).at(-1),'cell:measure:99.99');await finish(3)
 assert.match(await guide.textContent(),/Mass used for this measurement: 99\.99 g/)
 assert.ok(!(await actions()).includes('cell:measure:500'))
 const beforeNavigation=(await actions()).length
 await page.evaluate(()=>location.hash='files');await card.waitFor({state:'detached'})
 await page.evaluate(()=>location.hash='bed/cell');await guide.waitFor()
 await page.waitForFunction(()=>document.querySelector('.cc2-cell-guide input').value==='99.99')
 assert.equal((await actions()).length,beforeNavigation,'returning to a pending guide must restore the actual measured mass without sending a command')
 assert.match(await guide.textContent(),/25\.125 \/ —/,'a broken native TARE report must not become a numeric reference')
 await mass.fill('600');assert.equal(await button('Apply calibration').isDisabled(),true,'changed mass requires a new measurement')
 await mass.fill('99.99')
 const gets=await page.evaluate(()=>window.__GETS)
 await page.waitForTimeout(1100)
 assert.equal(await page.evaluate(()=>window.__GETS),gets,'waiting for user input must not poll the native tool status')
 await click('Apply calibration',true);assert.equal((await actions()).at(-1),'cell:accept');await finish(4)
 assert.match(await guide.textContent(),/25\.125 \/ -125/,'the reference is shown only after verified native acceptance')
 assert.equal(await button('Abort calibration').count(),0,'ABORT must not pretend to undo accepted coefficients')
 await click('Save active value',true);assert.equal((await actions()).at(-1),'cell:save');await finish(5)
 assert.equal(await guide.getByRole('button',{name:'Restart printer',exact:true}).count(),1)
 await click('Restart printer',true);assert.equal((await actions()).at(-1),'cell:restart');await finish(5)
 for(const width of [1440,768,390]){
  await page.setViewportSize({width,height:1200});await page.waitForTimeout(150)
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`no overflow at ${width}`)
 }
 await page.evaluate(()=>{window.__CC2_PREVIEW.status=2})
 await page.waitForFunction(()=>[...document.querySelectorAll('.cc2-native-tools button')].every(b=>b.disabled))
 assert.deepEqual(errors,[])
 console.log('PASS: blank initial mass, confirmed exact 99.99 g request, cancellation and resumed mass, actual inline tare/guide controls, confirmations, incomplete acceptance and changed-mass guards, no waiting-state polling, preserved card column, emergency-independent printing guards and three viewport sizes')
}finally{await browser.close();await demo.cleanup()}
