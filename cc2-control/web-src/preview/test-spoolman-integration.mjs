import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir } from 'node:fs/promises'
const root=fileURLToPath(new URL('..',import.meta.url))
const server=await createServer({root,configFile:path.join(root,'vite.config.ts'),mode:'demo',server:{port:0,host:'127.0.0.1'}})
let browser
try {
 await server.listen()
 browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH}:{})})
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 const state={enabled:false,url:'http://192.0.2.5:7912',auth:true},writes=[],errors=[]
 let reject=false
 page.on('pageerror',e=>errors.push(e.message))
 await page.route('**/api/preferences',r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.route('**/api/spools',async r=>{const response=await r.fetch();const data=await response.json();Object.assign(data,{spoolman_enabled:state.enabled,spoolman_url:state.url,spoolman_has_auth:state.auth});if(data.spools?.length)data.spools[0].spoolman_id='7';await r.fulfill({json:data})})
 await page.route('**/api/spoolman',r=>{const body=r.request().postData();writes.push(body);if(reject)return r.fulfill({status:500,json:{error:'Simulated save failure'}});const flag=body.match(/^enabled=([01])$/m);if(flag)state.enabled=flag[1]==='1';return r.fulfill({json:{ok:true}})})
 const origin=server.resolvedUrls.local[0]
 await page.goto(origin+'#spools')
 await page.getByRole('heading',{name:'Inventory',exact:true}).waitFor()
 assert.equal(await page.getByRole('heading',{name:'Spoolman sync',exact:true}).count(),0)
 await page.goto(origin+'#settings/integrations')
 const toggle=page.getByRole('switch',{name:'Enable Spoolman',exact:true})
 await toggle.waitFor();assert.equal(await toggle.getAttribute('aria-checked'),'false')
 await toggle.click();await page.waitForFunction(()=>document.querySelector('[aria-label="Enable Spoolman"]').getAttribute('aria-checked')==='true')
 assert.deepEqual(writes,['enabled=1'])
 await page.goto(origin+'#spools');await page.getByRole('heading',{name:'Spoolman sync',exact:true}).waitFor()
 assert.equal(await page.getByPlaceholder('http://192.168.1.50:7912').inputValue(),state.url)
 await page.goto(origin+'#settings/integrations');await toggle.waitFor();reject=true;await toggle.click();await page.getByText('Simulated save failure',{exact:true}).waitFor();assert.equal(await toggle.getAttribute('aria-checked'),'true');reject=false
 await toggle.click();await page.waitForFunction(()=>document.querySelector('[aria-label="Enable Spoolman"]').getAttribute('aria-checked')==='false')
 await page.reload();await toggle.waitFor();assert.equal(await toggle.getAttribute('aria-checked'),'false');assert.equal(state.url,'http://192.0.2.5:7912');assert.equal(state.auth,true)
 await page.goto(origin+'#spools');await page.getByRole('heading',{name:'Inventory',exact:true}).waitFor();assert.equal(await page.getByRole('heading',{name:'Spoolman sync',exact:true}).count(),0)
 for(const width of [1440,390]){await page.setViewportSize({width,height:1000});await page.goto(origin+'#settings/integrations');await toggle.waitFor();await page.waitForTimeout(250);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'no horizontal overflow');await mkdir(path.join(root,'preview/screenshots'),{recursive:true});await page.screenshot({path:path.join(root,`preview/screenshots/spoolman-toggle-${width}.png`),fullPage:true})}
 assert.deepEqual(errors,[])
 console.log('PASS: default hidden, explicit activation, retained URL/auth, save refusal, disabled state survives reload and responsive settings')
}finally{await browser?.close();await server.close()}
