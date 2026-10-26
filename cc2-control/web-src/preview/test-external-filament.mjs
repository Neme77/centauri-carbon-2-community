import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
const server = await createServer({ root: new URL('..', import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser
let idle = true, present = false, fail = false
const actions = [], errors = []
try {
 await server.listen()
 browser = await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH}:{})})
 const page = await browser.newPage({viewport:{width:1440,height:1000}})
 page.on('pageerror',e=>errors.push(e.message))
 await page.route('**/api/preferences',r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.route('**/api/printer',async r=>{const response=await r.fetch();const d=await response.json();d.connected=true;d.machine.status=idle?1:2;d.machine.status_name=idle?'Idle':'Printing';d.print.enabled=!idle;d.print.state=idle?'':'printing';return r.fulfill({json:d})})
 await page.route('**/api/canvas',async r=>{const response=await r.fetch();const d=await response.json();(d.telemetry || d).result.canvas_info.canvas_list[0].connected=present?1:0;return r.fulfill({json:d})})
 await page.route('**/api/external-filament',r=>fail?r.fulfill({status:503,json:{error:'Unavailable'}}):r.fulfill({json:{available:!present,idle,filament:{manufacturer:'Generic',type:'TPU',detailed_type:'TPU 95A',color:'0xF72221'}}}))
 await page.route('**/api/control',r=>{actions.push(r.request().postData());return r.fulfill({json:{accepted:true}})})
 await page.goto(server.resolvedUrls.local[0]+'#control')
 const card=page.locator('[data-external-filament]')
 await card.waitFor()
 await card.getByRole('button',{name:'Edit material',exact:true}).click()
 const form=page.locator('[data-filament-material]')
 assert.equal(await form.getByLabel('Brand',{exact:true}).inputValue(),'1')
 assert.equal(await form.getByLabel('Type',{exact:true}).inputValue(),'3')
 assert.equal(await form.getByLabel('Subcategory',{exact:true}).inputValue(),'1')
 assert.equal(await form.getByRole('button',{name:'#F72221',exact:true}).getAttribute('aria-pressed'),'true')
 await form.getByLabel('Brand',{exact:true}).selectOption('0')
 assert.equal(await form.getByLabel('Type',{exact:true}).inputValue(),'0')
 assert.equal(await form.getByLabel('Subcategory',{exact:true}).inputValue(),'0')
 await form.getByLabel('Type',{exact:true}).selectOption('3')
 await form.getByRole('button',{name:'#09CC3A',exact:true}).click()
 await form.getByRole('button',{name:'Save',exact:true}).click()
 await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>!document.querySelector('[data-filament-material]'))
 assert.deepEqual(actions,['external:material:0:3:0:09CC3A'])
 for(const action of ['Load','Unload']){
  await card.getByRole('button',{name:action,exact:true}).click()
  await page.getByRole('button',{name:'Confirm',exact:true}).click()
 }
 assert.deepEqual(actions.slice(1),['external:load','external:unload'])
 await mkdir('preview/artifacts',{recursive:true})
 for(const width of [1440,390]){
  await page.setViewportSize({width,height:1000})
  await card.getByRole('button',{name:'Edit material',exact:true}).click()
  await page.screenshot({path:`preview/artifacts/external-filament-${width}.png`,fullPage:false})
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'no overflow')
  await form.getByRole('button',{name:'Cancel',exact:true}).click()
 }
 idle=false
 await page.waitForTimeout(5500)
 assert(await card.getByRole('button',{name:'Load',exact:true}).isDisabled())
 assert(await card.getByRole('button',{name:'Unload',exact:true}).isDisabled())
 assert(await card.getByRole('button',{name:'Edit material',exact:true}).isDisabled())
 idle=true;fail=true
 await page.waitForTimeout(5500)
 assert(await card.getByRole('button',{name:'Load',exact:true}).isDisabled(),'failed readback revokes actions')
 fail=false;present=true
 await page.waitForTimeout(5500)
 assert.equal(await card.count(),0,'Canvas connection hides external mode')
 await page.goto(server.resolvedUrls.local[0]+'#canvas')
 await page.getByRole('button',{name:"Select Material",exact:true}).click()
 await form.waitFor()
 await form.getByLabel('Brand',{exact:true}).selectOption('1')
 await form.getByLabel('Type',{exact:true}).selectOption('1')
 await form.getByLabel('Subcategory',{exact:true}).selectOption('1')
 await form.getByRole('button',{name:'#2850DF',exact:true}).click()
 await form.getByRole('button',{name:'Save',exact:true}).click()
 await page.getByRole('button',{name:'Confirm',exact:true}).click()
 assert.equal(actions.at(-1),'canvas:catalog:0:1:1:1:2850DF')
 assert.deepEqual(errors,[])
 console.log('PASS: native material fields, dependent choices, palette, confirmed external commands, busy/lost/Canvas guards, desktop/mobile')
} finally { await browser?.close();await server.close() }
