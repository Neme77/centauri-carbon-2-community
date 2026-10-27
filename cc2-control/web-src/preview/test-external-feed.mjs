import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
const root=fileURLToPath(new URL('..',import.meta.url))
const server=await createServer({root,mode:'demo',server:{host:'127.0.0.1',port:0}})
let browser,source='canvas',printing=false,fail=false
const choices=[],actions=[],errors=[]
try {
 await server.listen()
 browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH}:{})})
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 page.on('pageerror',e=>errors.push(e.message))
 await page.route('**/api/preferences',r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.route('**/api/printer',async r=>{
  const response=await r.fetch(),d=await response.json()
  d.connected=true;d.last_message_age=0;d.machine.status=printing?2:1;d.machine.status_name=printing?'Printing':'Idle'
  d.print.state=printing?'printing':'complete';d.print.filename=printing?'part.gcode':''
  d.motion.homed_axes=''
  return r.fulfill({json:d})
 })
 await page.route('**/api/external-filament',r=>r.fulfill({json:{available:true,experiment:true,source,can_select:!printing,
  idle:!printing&&source==='external',can_unload:false,filament:{manufacturer:'Generic',type:'TPU',detailed_type:'TPU 95A',color:'0xF72221'}}}))
 await page.route('**/api/filament-source',r=>{
  choices.push(r.request().postData())
  if(fail)return r.fulfill({status:409,json:{error:'Native confirmation unavailable'}})
  source=r.request().postData();return r.fulfill({json:{source}})
 })
 await page.route('**/api/control',r=>{actions.push(r.request().postData());return r.fulfill({json:{accepted:true}})})
 await page.goto(server.resolvedUrls.local[0]+'#canvas')
 const card=page.locator('[data-external-filament]')
 await card.waitFor()
 assert(await card.getByRole('button',{name:'Load',exact:true}).isDisabled())
 await card.getByRole('button',{name:'External spool',exact:true}).click()
 await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('[data-filament-source] button[aria-pressed=true]')?.textContent.includes('External'))
 assert.deepEqual(choices,['external'])
 assert.deepEqual(actions,[],'Source choice must not load or heat')
 assert(await page.locator('[data-canvas-action="canvas.load"]').isDisabled())
 await card.getByRole('button',{name:'Edit material',exact:true}).click()
 const form=page.locator('[data-filament-material]');await form.waitFor()
 assert.equal(await form.getByLabel('Type',{exact:true}).inputValue(),'3')
 await form.getByLabel('Type',{exact:true}).selectOption('0')
 await form.getByRole('button',{name:'Save',exact:true}).click()
 await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
 await form.waitFor({state:'detached'})
 assert(actions[0].startsWith('external:material:'),'Material uses the external profile with Canvas still connected')
 await page.reload();await card.waitFor()
 assert.equal(await card.getByRole('button',{name:'External spool',exact:true}).getAttribute('aria-pressed'),'true')
 assert(await card.getByRole('button',{name:'Unload',exact:true}).isDisabled(),'Do not unload an empty head')
 printing=true;await page.waitForTimeout(5600)
 assert(await card.getByRole('button',{name:'Load',exact:true}).isDisabled())
 assert(await card.getByRole('button',{name:'Canvas',exact:true}).isDisabled())
 printing=false;await page.waitForTimeout(5600)
 assert(await card.getByRole('button',{name:'Edit material',exact:true}).isEnabled(),'Card remains editable after completion without home')
 await mkdir('preview/artifacts',{recursive:true})
 for(const width of [1440,390]){
  await page.setViewportSize({width,height:1000})
  await page.waitForTimeout(500)
  const layout=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth,over:[...document.querySelectorAll('body *')].filter(e=>e.getBoundingClientRect().right>innerWidth+1).slice(0,8).map(e=>({tag:e.tagName,class:e.className,width:e.getBoundingClientRect().width,text:e.textContent.slice(0,70)}))}))
  assert(layout.scroll<=width+1,JSON.stringify(layout))
  await card.screenshot({path:`preview/artifacts/external-feed-${width}.png`})
 }
 fail=true
 await card.getByRole('button',{name:'Canvas',exact:true}).click();await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
 assert.equal(await card.getByRole('button',{name:'External spool',exact:true}).getAttribute('aria-pressed'),'true','Failed command keeps the previous choice')
 fail=false
 await card.getByRole('button',{name:'Canvas',exact:true}).click();await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('[data-filament-source] button[aria-pressed=true]')?.textContent.includes('Canvas'))
 assert(await card.getByRole('button',{name:'Load',exact:true}).isDisabled())
 assert.deepEqual(errors,[])
 console.log('PASS external selection, confirmation, no source-only motion, material editing, reload/print completion, busy/failure guards and mobile')
}finally{await browser?.close();await server.close()}
