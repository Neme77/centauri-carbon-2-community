import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture(`
window.__CC2_PREVIEW.preferences.language='en';
const original=window.fetch;
window.__TOOLS={busy:false,generation:0,error:'none',mode:0,active:null,original:null,configured:28.8,saved:null,editable:true,pending_restart:false,pending_conflict:false,reports:[]};
window.__TOOL_ACTIONS=[];
window.__TOOLS_NATIVE='standby';
window.fetch=async(input,options={})=>{
 const path=String(input);
 const reply=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'Content-Type':'application/json'}});
 if(path.endsWith('/api/calibration-tools/status'))return reply(window.__TOOLS);
 if(path.endsWith('/api/calibration-tools/action')){
  window.__TOOL_ACTIONS.push(String(options.body));
  window.__TOOLS.busy=true;window.__TOOLS.mode=String(options.body)==='rotation:read'?1:String(options.body).startsWith('rotation:apply:')?2:String(options.body).startsWith('rotation:save:')?3:String(options.body)==='cell:read'?4:5;window.__TOOLS.generation++;return reply({accepted:true},202);
 }
 const response=await original(input,options);
 if(path.endsWith('/api/printer')){const data=await response.json();data.print.native={state:window.__CC2_PREVIEW.status===2?'printing':window.__TOOLS_NATIVE};return reply(data)}
 return response;
};
`)
const browser=await chromium.launch(launchOptions)
try{
 const page=await browser.newPage({viewport:{width:1440,height:1200}}),errors=[]
 page.on('pageerror',e=>errors.push(e.message))
 await page.goto(demo.url+'#control')
 const card=page.locator('.cc2-native-tools')
 const button=name=>card.getByRole('button',{name,exact:true})
 const actions=()=>page.evaluate(()=>window.__TOOL_ACTIONS)
 await button('Read active distance').waitFor()
 await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Read active distance').disabled)
 assert.equal(await button('Apply temporarily').isDisabled(),true)
 assert.equal((await actions()).length,0,'opening the card must not run any native command')
 await page.evaluate(()=>{window.__TOOLS_NATIVE='cancelled'})
 await page.waitForTimeout(1700)
 assert.equal(await button('Read active distance').isDisabled(),false,'Idle after cancellation must allow reading Rotation Distance')
 await button('Read active distance').click()
 await page.waitForFunction(()=>document.querySelector('.cc2-native-tools [role=status]').textContent.includes('running'))
 assert.equal(await button('Save active value').isDisabled(),true)
 await button('Read active distance').dispatchEvent('click')
 assert.deepEqual(await actions(),['rotation:read'])
 await page.evaluate(()=>Object.assign(window.__TOOLS,{busy:false,active:28.8,reports:["Extruder 'extruder' rotation distance set to 28.800000"]}))
 await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Read active distance').disabled)
 assert.equal(await button('Apply temporarily').isDisabled(),true,'a measurement must be entered after reading the baseline')
 // The ready render enables the button before Preact's after-paint effect
 // hydrates the read baseline and clears the previous measurement. Enter the
 // next measurement only after that completed-read render has been painted.
 await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(()=>setTimeout(resolve,0)))))
 const inputs=card.locator('input[type=number]')
 await inputs.nth(1).fill('95')
 await page.waitForFunction(()=>document.querySelector('.cc2-native-tools').textContent.includes('27.36 mm'))
 assert.match(await card.textContent(),/27.36 mm/)
 await inputs.nth(1).fill('-95')
 await page.waitForFunction(()=>[...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Apply temporarily').disabled)
 assert.equal(await button('Apply temporarily').isDisabled(),true)
 await inputs.nth(1).fill('95')
 await button('Apply temporarily').click()
 await page.getByRole('button',{name:'Cancel',exact:true}).click()
 assert.deepEqual(await actions(),['rotation:read'],'cancel must not apply a calibration')
 await button('Apply temporarily').click()
 await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>window.__TOOL_ACTIONS.length===2)
 assert.equal((await actions()).at(-1),'rotation:apply:28.8:27.3600000000')
 await page.evaluate(()=>Object.assign(window.__TOOLS,{busy:false,active:27.36,original:28.8,reports:["Extruder 'extruder' rotation distance set to 27.360000"]}))
 await page.waitForFunction(()=>![...document.querySelectorAll('.cc2-native-tools button')].find(b=>b.textContent==='Restore configured value').disabled)
 assert.equal(await inputs.nth(1).inputValue(),'','an old measurement must not be applied twice')
 assert.equal(await button('Apply temporarily').isDisabled(),true)
 await button('Save active value').click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>window.__TOOL_ACTIONS.length===3)
 assert.equal((await actions()).at(-1),'rotation:save:27.36')
 await page.evaluate(()=>Object.assign(window.__TOOLS,{busy:false,saved:27.36,pending_restart:true}))
 await page.waitForFunction(()=>document.querySelector('.cc2-native-tools').textContent.includes('Saved. Restart'))
 assert.equal(await button('Apply temporarily').isDisabled(),true)
 await button('Read active distance').click()
 await page.evaluate(()=>Object.assign(window.__TOOLS,{busy:false,active:27.36,pending_conflict:true}))
 await page.waitForFunction(()=>document.querySelector('.cc2-native-tools').textContent.includes('saved value changed'))
 assert.equal(await button('Restart printer').isDisabled(),true)
 await page.goto(demo.url+'#bed/cell')
 await button('Run diagnosis').click()
 await page.waitForFunction(()=>window.__TOOL_ACTIONS.at(-1)==='cell:diagnostic')
 await page.evaluate(()=>Object.assign(window.__TOOLS,{busy:false,error:'Native tool command refused',reports:['<script>not HTML</script>']}))
 await page.waitForFunction(()=>document.querySelector('.cc2-native-tools [role=status]').textContent.includes('refused'))
 assert.equal(await card.locator('pre script').count(),0)
 assert.match(await card.locator('pre').textContent(),/<script>/)
 await page.evaluate(()=>{window.__CC2_PREVIEW.status=2})
 await page.waitForFunction(()=>[...document.querySelectorAll('.cc2-native-tools button')].every(b=>b.disabled))
 await page.evaluate(()=>{window.__CC2_PREVIEW.status=1;window.__TOOLS_NATIVE='paused'})
 await page.waitForTimeout(1700)
 assert.equal(await button('Read load cell').isDisabled(),true,'an active native pause still refuses calibration')
 for(const width of [1440,768,390]){
  await page.setViewportSize({width,height:1200});await page.waitForTimeout(150);assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`no overflow at ${width}: ${JSON.stringify(await page.evaluate(()=>[...document.querySelectorAll('.cc2-native-tools *')].filter(e=>e.getBoundingClientRect().right>innerWidth).map(e=>({tag:e.tagName,cls:e.className,w:e.getBoundingClientRect().width,text:e.textContent.slice(0,35)})).slice(0,12)))}`)
 }
 assert.deepEqual(errors,[])
 console.log('PASS: actual native-tools buttons, no automatic hardware requests, pending/confirmation, correct calculator/API arguments, save/restart conflict, native output text and printing guards')
}finally{await browser.close();await demo.cleanup()}
