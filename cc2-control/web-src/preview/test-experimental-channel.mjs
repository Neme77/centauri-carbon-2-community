import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

const root=fileURLToPath(new URL('..',import.meta.url))
const server=await createServer({root,configFile:path.join(root,'vite.config.ts'),mode:'demo',server:{port:0,host:'127.0.0.1'}})
let browser
try {
 await server.listen()
 browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH}:{})})
 for(const language of ['en','it']) for(const width of [1440,390]) {
  const context=await browser.newContext({viewport:{width,height:1100}})
  const page=await context.newPage(), errors=[], downloads=[], featureWrites=[], native=[]
  const dict=JSON.parse(await readFile(path.join(root,`public/locales/${language}.json`),'utf8'))
  page.on('pageerror',e=>errors.push(e.message))
  await page.addInitScript(()=>localStorage.setItem('cc2-update-channel','canvas-uds'))
  const release=(version,channel,notes)=>({version,channel,firmware:'4.4',url:`https://github.com/Neme77/centauri-carbon-2-community/releases/download/test-${version}/Control.cc2update`,size:1500000,notes})
  const index={schema:1,expires:Math.floor(Date.now()/1000)+86400,releases:[release('1.2.20','stable','Stable channel fixture'),release('1.2.21','canvas-uds','Explicit experimental fixture')]}
  await page.route('**/api/preferences',r=>r.fulfill({json:{language,theme:'dark'}}))
  await page.route('**/api/health',r=>r.fulfill({json:{version:'1.2.19',mqtt_connected:true,mqtt_registered:true,snapshot_received:true}}))
  await page.route('**/api/app-update/status',r=>r.fulfill({json:{enabled:true,ready:true,busy:false,installed:'1.2.19',firmware:'4.4',phase:'idle',job:'',version:''}}))
  await page.route('**/api/app-update/catalog',r=>r.fulfill({json:index}))
  await page.route('https://raw.githubusercontent.com/**/update-channel.cc2catalog',r=>r.fulfill({body:'signed-catalog-fixture',headers:{'Access-Control-Allow-Origin':'*'}}))
  await page.route('**/api/app-update/download',r=>{downloads.push(r.request().postData());return r.fulfill({status:503,json:{error:'Fixture download refusal'}})})
  await page.route('**/api/experiments',r=>{
   if(r.request().method()==='POST')featureWrites.push(r.request().postData())
   return r.fulfill({json:{schema:1,editable:true,ready:true,canvas_uds:false,canvas_uds_available:false,quiet_motors:false,normal_confirmed:false,cli_canvas:false}})
  })
  await page.route('**/api/control',r=>{native.push(r.request().postData());return r.fulfill({status:409,json:{error:'No command expected'}})})
  await page.goto(server.resolvedUrls.local[0]+'#settings/updates')
  const selector=page.getByLabel(dict['updates.channel'],{exact:true})
  await selector.waitFor()
  assert.equal(await selector.inputValue(),'stable','legacy preference cannot silently opt into experimental builds')
  await page.getByText('Stable channel fixture',{exact:true}).waitFor()
  const confirm=()=>page.getByRole('dialog').getByRole('button',{name:dict['common.confirm'],exact:true}).click()
  const cancel=()=>page.getByRole('dialog').getByRole('button',{name:dict['common.cancel'],exact:true}).click()
  await selector.selectOption('canvas-uds');await cancel();assert.equal(await selector.inputValue(),'stable')
  await selector.selectOption('canvas-uds');await confirm()
  await page.getByText('Explicit experimental fixture',{exact:true}).waitFor()
  assert.equal(await selector.inputValue(),'canvas-uds')
  assert.equal(await page.locator('[data-experiments] [aria-checked=true]').count(),0)
  assert.deepEqual(downloads,[]);assert.deepEqual(featureWrites,[]);assert.deepEqual(native,[])
  await page.reload();await page.getByText('Explicit experimental fixture',{exact:true}).waitFor()
  assert.equal(await selector.inputValue(),'canvas-uds')
  await page.getByLabel(dict['settings.lan_access_code'],{exact:true}).fill('fixture-confirmation')
  const download=page.getByRole('button',{name:dict['updates.download'],exact:true})
  await download.click();await cancel();assert.deepEqual(downloads,[])
  await download.click();await confirm();await page.getByRole('alert').filter({hasText:'Fixture download refusal'}).waitFor()
  assert.deepEqual(downloads,[index.releases[1].url])
  await page.locator('[data-update-notice]').click()
  await page.getByRole('dialog').getByRole('button',{name:dict['updates.open'],exact:true}).click()
  await page.getByText('Stable channel fixture',{exact:true}).waitFor()
  assert.equal(await selector.inputValue(),'stable','stable notification opens the stable view')
  assert.deepEqual(featureWrites,[]);assert.deepEqual(native,[])
  for(const theme of ['dark','light']) {
   await page.evaluate(t=>document.documentElement.dataset.theme=t,theme)
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth))
  }
  assert.deepEqual(errors,[])
  await context.close()
 }
 console.log('PASS explicit experimental selection/cancel, independent feature opt-ins, persistence, exact download URL, stable-only bell and responsive EN/IT controls')
}finally{await browser?.close();await server.close()}
