import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture(`
window.__CC2_PREVIEW.preferences.language='en';
const original=window.fetch;
window.__TOOLS={busy:false,generation:0,error:'none',mode:1,active:null,original:null,configured:28.8,saved:29.088,editable:true,pending_restart:false,pending_conflict:false,reports:[]};
window.__ACTIONS=[];
window.fetch=async(input,options={})=>{
 const path=String(input),reply=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'Content-Type':'application/json'}});
 if(path.endsWith('/api/calibration-tools/status'))return reply(window.__TOOLS);
 if(path.endsWith('/api/calibration-tools/action')){
  const action=String(options.body);window.__ACTIONS.push(action);window.__TOOLS.generation++;
  if(action==='rotation:read'){window.__TOOLS.active??=29.088;window.__TOOLS.mode=1}
  else if(action.startsWith('rotation:apply:')){window.__TOOLS.original??=window.__TOOLS.active;window.__TOOLS.active=Number(action.split(':')[3]);window.__TOOLS.mode=2}
  else if(action.startsWith('rotation:save:')){window.__TOOLS.saved=window.__TOOLS.active;window.__TOOLS.pending_restart=true;window.__TOOLS.mode=3}
  else throw new Error('Unexpected mutation '+action);
  return reply({accepted:true},202);
 }
 const response=await original(input,options);
 if(path.endsWith('/api/printer')){const data=await response.json();data.print.native={state:window.__CC2_PREVIEW.status===2?'printing':'standby'};return reply(data)}
 return response;
};
`)
const browser=await chromium.launch(launchOptions)
try{
 const page=await browser.newPage({viewport:{width:1440,height:1200}}),errors=[]
 page.on('pageerror',e=>errors.push(e.message))
 await page.goto(demo.url+'#control')
 const card=page.locator('.cc2-native-tools'),button=name=>card.getByRole('button',{name,exact:true})
 const actions=()=>page.evaluate(()=>window.__ACTIONS)
 await button('Restore configured value').waitFor()
 assert.equal(await button('Restore configured value').isDisabled(),true,'read the live distance before restoration')
 await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Read active distance').disabled)
 await button('Read active distance').click()
 await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Restore configured value').disabled)
 assert.equal(await page.evaluate(()=>window.__TOOLS.original),null,'reproduce the exact post-reboot state')
 await button('Restore configured value').click()
 assert.match(await page.getByRole('dialog').textContent(),/28\.8 mm/)
 await page.getByRole('button',{name:'Cancel',exact:true}).click()
 assert.deepEqual(await actions(),['rotation:read'])
 async function restore(expected){
  await button('Restore configured value').click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
  await page.waitForFunction(expected=>window.__ACTIONS.at(-1)===expected,expected)
  await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Restore configured value').disabled)
 }
 await restore('rotation:apply:29.088:28.8')
 assert.equal(await page.evaluate(()=>window.__TOOLS.original),29.088,'the temporary undo value differs from configured')
 await restore('rotation:apply:28.8:28.8')
 assert.ok(!(await actions()).some(a=>a.startsWith('rotation:save:')),'restore must never save automatically')
 await button('Save active value').click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>window.__ACTIONS.at(-1)==='rotation:save:28.8')
 await page.waitForFunction(()=>[...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Restore configured value').disabled)
 await page.evaluate(()=>Object.assign(window.__TOOLS,{pending_restart:false,configured:null,active:29.088,original:29.088}))
 await button('Read active distance').click()
 await page.waitForFunction(()=>document.querySelector('.cc2-native-tools').textContent.includes('Configured—'))
 assert.equal(await button('Restore configured value').isDisabled(),true,'never replace an unknown configured value with a session undo value')
 await page.evaluate(()=>window.__CC2_PREVIEW.status=2)
 await page.waitForFunction(()=>[...document.querySelectorAll('.cc2-native-tools button')].every(b=>b.disabled))
 for(const width of [1440,768,390]){
  await page.setViewportSize({width,height:1200});await page.waitForTimeout(150)
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`no overflow at ${width}`)
 }
 assert.deepEqual(errors,[])
 console.log('PASS: real restore button with post-reboot null original, configured 28.8 target, no toggling to 29.088, cancellation, separate save, unavailable-config/printing guards and responsive layout')
}finally{await browser.close();await demo.cleanup()}
