import assert from 'node:assert/strict'
import {createServer} from 'vite'
import {chromium} from 'playwright'
import {fileURLToPath} from 'node:url'
import path from 'node:path'
const root=fileURLToPath(new URL('..',import.meta.url))
const server=await createServer({root,configFile:path.join(root,'vite.config.ts'),mode:'demo',server:{port:0,host:'127.0.0.1'}})
let browser
try{
 await server.listen()
 browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH}:{})})
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 const origin=server.resolvedUrls.local[0]
 await page.goto(origin+'#dashboard');await page.locator('.cc2-dashboard').waitFor()
 await page.getByTitle('Collapse menu',{exact:true}).click();await page.waitForTimeout(250)
 assert.equal(await page.locator('#cc2-navigation').evaluate(e=>e.getBoundingClientRect().width),62)
 const icon=page.locator('#cc2-navigation button[title="Expand menu"] svg')
 assert.equal(await icon.evaluate(e=>e.getBoundingClientRect().width),22,'expand icon must not shrink behind the retained label')
 for(const [width,height] of [[320,844],[390,844],[768,1024],[900,1100],[900,450]]){
  await page.setViewportSize({width,height});await page.reload()
  await page.locator('#cc2-menu-toggle').click();await page.waitForTimeout(250)
  const labels=await page.locator('#cc2-navigation nav a span').evaluateAll(es=>es.map(e=>getComputedStyle(e).visibility))
  assert(labels.length>=10&&labels.every(v=>v==='visible'),'saved desktop collapse must never hide mobile drawer labels')
  await page.keyboard.press('Escape')
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false)
 }
 await page.goto(origin+'#canvas');await page.locator('.cc2-canvas-ring').first().waitFor()
 for(const width of [320,390,768,1440]){
  await page.setViewportSize({width,height:1000});await page.waitForTimeout(100)
  const rings=await page.locator('.cc2-canvas-ring').evaluateAll(es=>es.map(e=>({outer:e.getBoundingClientRect().width,centre:e.clientWidth})))
  assert.equal(rings.length,4)
  assert(rings.every(r=>r.outer===76&&r.centre===34),'Canvas ring outer size and inner hole')
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false)
 }
 console.log('PASS: centred collapsed icons, visible expand icon, persisted-collapse mobile drawer labels and responsive Canvas rings')
}finally{await browser?.close();await server.close()}
