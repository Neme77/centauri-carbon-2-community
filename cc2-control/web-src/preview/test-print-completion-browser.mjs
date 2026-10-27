import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
const root=fileURLToPath(new URL('..',import.meta.url))
const server=await createServer({root,mode:'demo',server:{host:'127.0.0.1',port:0}})
let browser
let state='printing',uuid='job-1',connected=true,cleared=false
const writes=[],errors=[]
try{
 await server.listen();browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH}:{})})
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 page.on('pageerror',e=>errors.push(e.message))
 await page.route('**/api/preferences',r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.route('**/api/printer',async r=>{
  const response=await r.fetch(),d=await response.json(),printing=['printing','paused'].includes(state)
  d.connected=connected;d.last_message_age=connected?0:60;d.machine={status:printing?2:1,status_name:printing?'Printing':'Idle',sub_status:0,progress:printing?43:100}
  d.print={enabled:printing,filename:printing?'part.gcode':'',state:printing?state:'',uuid:printing?uuid:'',duration:printing?80:0,current_layer:printing?4:0,total_layers:10,remaining:printing?20:0,native:printing?null:{state,filename:cleared?'':'local/part.gcode',duration:cleared?null:123.5}}
  d.motion={...d.motion,homed_axes:'',x:52.5,y:264,z:80}
  return r.fulfill({json:d})
 })
 await page.route('**/api/gcode-files/inspect',r=>{assert.equal(r.request().postData(),'internal\npart.gcode');return r.fulfill({json:{tools:[0],filaments:[],bed_temperature:60,nozzle_diameter:0.4}})})
 await page.route('**/api/control',r=>{writes.push(r.request().postData());return r.fulfill({status:409,json:{error:'No printer command expected'}})})
 await page.goto(`${server.resolvedUrls.local[0]}#job`)
 await page.getByText('part.gcode',{exact:true}).first().waitFor()
 const notice=page.locator('[data-print-completed]')
 assert.equal(await notice.count(),0)
 connected=false;state='complete';await page.waitForTimeout(3400);assert.equal(await notice.count(),0,'Disconnection is not completion')
 connected=true;await notice.waitFor();await notice.getByRole('heading',{name:'Print completed',exact:true}).waitFor()
 assert.deepEqual(writes,[])
 await page.waitForFunction(()=>[...document.querySelectorAll('[role=progressbar]')].every(e=>e.getAttribute('aria-valuenow')==='0'))
 await notice.getByRole('button',{name:'Confirm',exact:true}).click();await notice.waitFor({state:'detached'})
 await page.reload();await page.waitForTimeout(1000);assert.equal(await notice.count(),0)
 state='printing';uuid='job-2';await page.getByText('part.gcode',{exact:true}).first().waitFor();await page.waitForTimeout(2000)
 state='cancelled';await page.waitForTimeout(2000);assert.equal(await notice.count(),0,'Cancelled must not be successful')
 state='printing';uuid='job-3';await page.waitForTimeout(2000);cleared=true;state='complete';await notice.waitFor()
 await page.addStyleTag({content:'#cc2-preview-banner{display:none!important}'})
 await page.setViewportSize({width:390,height:844});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1))
 await mkdir('preview/artifacts',{recursive:true});await notice.screenshot({path:'preview/artifacts/print-completed-mobile.png'})
 await page.setViewportSize({width:1440,height:1000});await notice.screenshot({path:'preview/artifacts/print-completed-desktop.png'})
 await notice.getByRole('button',{name:'Print again',exact:true}).click()
 await page.getByRole('dialog').waitFor();assert.deepEqual(writes,[],'Replay must open setup, never print directly')
 assert.deepEqual(errors,[]);console.log('PASS nonblocking completion, reconnect, idle progress reset, acknowledgement/reload dedup, cancellation, repeat print setup and phone layout')
}finally{await browser?.close();await server.close()}
