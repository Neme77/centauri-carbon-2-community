import { chromium } from 'playwright'
import assert from 'node:assert/strict'
import { fixture, launchOptions } from './fixture.mjs'
const setup = `
window.__RFID={installed:true,ready:true,busy:false,armed:false,state:'idle',generation:0,preview:null,actions:[]};
const originalFetch=window.fetch;
window.fetch=async function(input,options={}) {
 const url=typeof input==='string'?input:input.url;
 if(url==='/api/rfid')return new Response(JSON.stringify(window.__RFID),{headers:{'Content-Type':'application/json'}});
 if(url==='/api/rfid/action'){
  const r=window.__RFID,action=options.body;r.actions.push(action);
  if(action==='scan'){r.generation++;r.busy=true;r.armed=false;r.state='scanning';r.preview=null;}
  if(action==='cancel'){r.busy=false;r.state='cancelled';r.preview=null;}
  if(action.startsWith('apply:')){r.preview=null;r.state='submitted';}
  return new Response('{"accepted":true}',{headers:{'Content-Type':'application/json'}});
 }
 return originalFetch(input,options);
};
window.__RFID_PREVIEW={uid:'1122334455667788',brand:'Anycubic',material:'PETG',sku:'AHPELG-102',color:'#78D64B',diameter:1.75,nozzle_min:230,nozzle_max:250,bed_min:60,bed_max:70,weight:null,remaining:null};
`
const demo=await fixture(setup)
const browser=await chromium.launch(launchOptions)
try {
 const page=await browser.newPage({viewport:{width:1280,height:1000}})
 await page.goto(demo.url+'#canvas')
 await page.locator('[data-rfid-toggle]').waitFor()
 assert.equal(await page.locator('[data-rfid-toggle]').getAttribute('aria-expanded'),'false')
 assert.equal(await page.locator('[data-rfid-start]').isVisible(),false)
 await page.locator('[data-rfid-toggle]').click()
 await page.locator('[data-rfid-start]').click()
 assert.deepEqual(await page.evaluate(()=>window.__RFID.actions),['scan'])
 await page.waitForFunction(()=>document.querySelector('[data-rfid-state]').textContent.includes('Preparazione'))
 await page.evaluate(()=>{window.__RFID.armed=true})
 await page.waitForFunction(()=>document.querySelector('[data-rfid-state]').textContent.includes('60 secondi'))
 await page.evaluate(()=>Object.assign(window.__RFID,{busy:false,state:'preview',preview:window.__RFID_PREVIEW}))
 await page.locator('[data-rfid-preview]').waitFor()
 await page.locator('[data-rfid-toggle]').click()
 assert.equal(await page.locator('[data-rfid-preview]').isVisible(),false)
 await page.locator('[data-rfid-toggle]').click()
 assert((await page.locator('[data-rfid-preview]').textContent()).includes('PETG'))
 assert(await page.locator('[data-rfid-apply]').isDisabled())
 await page.locator('[data-rfid-slot]').selectOption('2')
 await page.locator('[data-rfid-apply]').click()
 await page.getByRole('button',{name:'Annulla',exact:true}).last().click()
 assert.deepEqual(await page.evaluate(()=>window.__RFID.actions),['scan'])
 await page.screenshot({path:'../../../../outputs/CC2-RFID-Panel-R1/rfid-preview.png',fullPage:true})
 await page.locator('[data-rfid-apply]').click()
 await page.getByRole('button',{name:'Conferma',exact:true}).click()
 await page.waitForFunction(()=>window.__RFID.actions.length===2)
 assert.deepEqual(await page.evaluate(()=>window.__RFID.actions),['scan','apply:1:2'])
 await page.waitForFunction(()=>!document.querySelector('[data-rfid-preview]'))
 await page.evaluate(()=>Object.assign(window.__CC2_PREVIEW.trays[2],{filament_type:'PETG',filament_color:'#78D64B',status:0}))
 await page.waitForFunction(()=>document.querySelector('[data-rfid-metadata]')?.textContent.includes('Riportati'))
 await page.waitForFunction(()=>document.querySelector('[data-rfid-presence]')?.textContent.includes('Non rilevato'))
 await page.evaluate(()=>{window.__CC2_PREVIEW.trays[2].status=1})
 await page.waitForFunction(()=>document.querySelector('[data-rfid-presence]')?.textContent.includes('Rilevato'))
 await page.locator('[data-rfid-start]').click()
 await page.locator('[data-rfid-cancel]').click()
 await page.waitForFunction(()=>!window.__RFID.busy)
 await page.evaluate(()=>{Object.assign(window.__RFID,{ready:false,reason:'nozzle_hot',state:'state_changed',preview:null});window.__CC2_PREVIEW.status=2})
 await page.waitForFunction(()=>document.querySelector('[data-rfid-start]').disabled)
 await page.waitForFunction(()=>document.querySelector('[data-rfid-blocked]')?.textContent.includes('50 °C'))
 await page.evaluate(()=>{window.__RFID.error='operation_locked'})
 await page.waitForFunction(()=>document.querySelector('[data-rfid-error]')?.textContent.includes('operation_locked'))
 console.log('PASS RFID browser controls: arming, preview, explicit slot, cancelled confirmation, one metadata request, scan cancel and busy printer refusal.')
} finally {await browser.close();await demo.cleanup()}
