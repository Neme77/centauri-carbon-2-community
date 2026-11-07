import { chromium } from 'playwright'
import assert from 'node:assert/strict'
import { fixture, launchOptions } from './fixture.mjs'
const setup = `
window.__RFID={installed:true,ready:true,busy:false,armed:false,state:'idle',generation:0,preview:null,actions:[]};
window.__RFID_NATIVE={installed:false,metadata_valid:false,phase:-1,busy:false,reboot_required:false,recovery_required:false,retry_required:false,actions:[]};
if(sessionStorage.getItem('rfid-reboot-fixture'))Object.assign(window.__RFID_NATIVE,{installed:true,metadata_valid:true,phase:1,reboot_required:true});
const originalFetch=window.fetch;
window.fetch=async function(input,options={}) {
 const url=typeof input==='string'?input:input.url;
 if(url==='/api/rfid')return new Response(JSON.stringify(window.__RFID),{headers:{'Content-Type':'application/json'}});
 if(url==='/api/rfid/lifecycle')return new Response(JSON.stringify(window.__RFID_NATIVE),{headers:{'Content-Type':'application/json'}});
 if(url==='/api/rfid/lifecycle/action'){
  window.__RFID_NATIVE.actions.push(options.body);window.__RFID_NATIVE.busy=true;
  return new Response('{"accepted":true}',{headers:{'Content-Type':'application/json'}});
 }
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
 await page.evaluate(()=>{
  window.__CC2_PREVIEW.status=1;
  Object.assign(window.__RFID_NATIVE,{installed:true,metadata_valid:true,phase:1,reboot_required:true});
 })
 const notice=page.locator('[data-rfid-reboot]')
 await notice.waitFor()
 await page.evaluate(()=>sessionStorage.setItem('rfid-reboot-fixture','1'))
 await page.reload()
 await notice.waitFor()
 assert((await notice.textContent()).includes('riavvio completo'))
 assert.equal(await page.locator('[data-canvas-firmware]').count(),0)
 await page.evaluate(()=>{
  window.__CC2_PREVIEW.connected=false;
  window.location.hash='dashboard';
  Object.assign(window.__RFID_NATIVE,{phase:4,reboot_required:true,busy:true});
 })
 await page.waitForFunction(()=>document.querySelector('.cc2-page-content')?.classList.contains('opacity-50'))
 assert.equal(await notice.count(),0)
 await page.evaluate(()=>{window.__RFID_NATIVE.busy=false})
 await notice.waitFor()
 assert((await notice.textContent()).includes('secondo riavvio completo'))
 assert.equal(await notice.evaluate(el=>!!el.closest('.opacity-50')),false)
 assert.equal(await page.locator('[data-rfid-scan]').count(),0)
 await page.evaluate(()=>Object.assign(window.__RFID_NATIVE,{phase:5,reboot_required:false}))
 await notice.waitFor({state:'detached'})
 await page.evaluate(()=>Object.assign(window.__RFID_NATIVE,{phase:7,reboot_required:true}))
 await notice.waitFor()
 assert((await notice.textContent()).includes('firmware Canvas originale completato'))
 await page.evaluate(()=>Object.assign(window.__RFID_NATIVE,{phase:8,reboot_required:false}))
 await notice.waitFor({state:'detached'})
 await page.evaluate(()=>{
  window.__CC2_PREVIEW.connected=true;
  window.location.hash='settings/integrations';
  Object.assign(window.__RFID_NATIVE,{phase:-1,busy:false});
 })
 const firmware=page.locator('[data-canvas-firmware]')
 const install=firmware.getByRole('button',{name:'Installa firmware Canvas modificato'})
 await install.click()
 await page.getByRole('button',{name:'Annulla',exact:true}).last().click()
 assert.deepEqual(await page.evaluate(()=>window.__RFID_NATIVE.actions),[])
 for(const [phase,label,action] of [
  [-1,'Installa firmware Canvas modificato','activate'],
  [5,'Ripristina applicazione Canvas originale','restore'],
  [8,'Ripristina file di avvio originali','remove'],
 ]){
  await page.evaluate(phase=>Object.assign(window.__RFID_NATIVE,{phase,busy:false}),phase)
  const button=firmware.getByRole('button',{name:label,exact:true})
  await button.click()
  await page.getByRole('button',{name:'Conferma',exact:true}).click()
  await page.waitForFunction(action=>window.__RFID_NATIVE.actions.at(-1)===action,action)
 }

 console.log('PASS RFID browser controls: scan/assignment/cancel guards, collapsed and reload-persistent reboot notice, explicit activation confirmation and one native action.')
} finally {await browser.close();await demo.cleanup()}
