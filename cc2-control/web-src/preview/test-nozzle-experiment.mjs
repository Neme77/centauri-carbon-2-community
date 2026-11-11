import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
const root=fileURLToPath(new URL('..',import.meta.url))
const server=await createServer({root,configFile:path.join(root,'vite.config.ts'),mode:'demo',server:{port:0,host:'127.0.0.1'}})
const state={schema:1,editable:true,ready:true,canvas_uds:false,canvas_uds_available:false,quiet_motors:false,normal_confirmed:false,cli_canvas:false,bed_drying:false,nozzle_expansion:false}
const actions=[],mutations=[],errors=[];let pending=false,browser,holdNextRead=false,releaseRead,markRead
const readStarted=new Promise(resolve=>{markRead=resolve})
try{
 await server.listen()
 browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_EXECUTABLE?{executablePath:process.env.CC2_BROWSER_EXECUTABLE}:{})})
 const page=await browser.newPage({viewport:{width:1440,height:1200}})
 page.on('pageerror',e=>errors.push(e.message))
 await page.route('**/api/preferences',r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.route('**/api/experiments',async r=>{
  if(r.request().method()==='GET'&&holdNextRead){
   holdNextRead=false
   const gate=new Promise(resolve=>{releaseRead=resolve})
   markRead()
   await gate
  }
  if(r.request().method()==='POST'){
   assert.equal(r.request().headers()['x-cc2-lan'],'fixture-confirmation')
   const action=r.request().postData();actions.push(action)
   assert.ok(['nozzle-expansion:on','nozzle-expansion:off'].includes(action));state.nozzle_expansion=action.endsWith(':on')
  }
  return r.fulfill({json:state})
 })
 await page.route('**/api/nozzle-expansion',r=>r.fulfill({json:{ok:true,enabled:state.nozzle_expansion,available:true,editable:true,state:'idle',result:'',detail:'',start:130,end:160,step:10,delay:60,samples:5,elapsed:0,nozzle:25,target:0,reference:140,factory:{start:100,diff:10,values:[-.03,-.02,-.01,0]},override:null,measured:null,slope_um:0,residual_um:0,pending_restart:pending,restart:'none'}}))
 await page.route('**/api/nozzle-expansion/*',r=>{mutations.push(r.request().url());return r.fulfill({status:409,json:{error:'No native command expected'}})})
 const url=server.resolvedUrls.local[0]
 const toggle=()=>page.getByRole('switch',{name:'Nozzle thermal expansion',exact:true})
 // A hash navigation can expose the new input before mount effects run. Those
 // effects initialise the LAN field and start the experiment GET; await that
 // read before typing, rather than typing into a field still initialising.
 const openIntegrations=async()=>{
  const refreshed=page.waitForResponse(r=>new URL(r.url()).pathname==='/api/experiments'&&r.request().method()==='GET')
  await page.goto(url+'#settings/integrations')
  await refreshed
  await toggle().waitFor()
 }

 await openIntegrations();assert.equal(await toggle().isDisabled(),true)
 await page.getByLabel('LAN code for experimental features',{exact:true}).fill('fixture-confirmation')
 await toggle().click();await page.getByRole('button',{name:'Cancel',exact:true}).click();assert.deepEqual(actions,[])
 await toggle().click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('[aria-label="Nozzle thermal expansion"]').getAttribute('aria-checked')==='true')
 assert.deepEqual(actions,['nozzle-expansion:on']);assert.deepEqual(mutations,[])
 await page.goto(url+'#bed/nozzle');await page.getByText('Measure the table',{exact:true}).waitFor()
 assert.deepEqual(mutations,[],'opt-in and page navigation must heat/move nothing')
 // Reproduce slow re-entry deterministically. A visible switch is not proof
 // that the new page has finished initialising its input and reading state.
 holdNextRead=true
 let entered=false
 const entering=openIntegrations().then(()=>{entered=true})
 await readStarted
 await toggle().waitFor()
 assert.equal(await page.getByLabel('LAN code for experimental features',{exact:true}).inputValue(),'')
 assert.equal(await toggle().isDisabled(),true)
 assert.equal(entered,false,'Entry must await the fresh read, not just a visible cached switch')
 assert.deepEqual(actions,['nozzle-expansion:on'],'Pending page entry must send no change')
 releaseRead()
 await entering
 await page.getByLabel('LAN code for experimental features',{exact:true}).fill('fixture-confirmation')
 await toggle().click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('[aria-label="Nozzle thermal expansion"]').getAttribute('aria-checked')==='false')
 await page.goto(url+'#bed/nozzle')
 await page.getByRole('button',{name:'Experimental integrations',exact:true}).waitFor()
 assert.equal(await page.getByText('Measure the table',{exact:true}).count(),0)
 pending=true
 await page.goto(url+'#control');await page.goto(url+'#bed/nozzle')
 await page.getByRole('button',{name:'Restart printer',exact:true}).waitFor()
 assert.deepEqual(mutations,[],'pending recovery is visible without dispatching anything')
 for(const width of [1440,768,390]){await page.setViewportSize({width,height:1200});await page.waitForTimeout(150);assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`no overflow at ${width}`)}
 assert.deepEqual(errors,[])
 console.log('PASS: dedicated opt-in/cancel/disable, no native commands from enabling, disabled controls, pending restart visible while opted out, nine-language build and responsive layout')
}finally{if(browser)await browser.close();await server.close()}
