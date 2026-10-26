import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture()
const browser=await chromium.launch(launchOptions)
try {
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 await page.goto(demo.url+'#control')
 await page.evaluate(()=>{
  const fetch=window.fetch
  const saved={points:[[0,0],[0,0]],mesh_params:{min_x:0,min_y:0,max_x:200,max_y:200}}
  const data={result:{status:{bed_mesh:{profile_name:'default1',mesh_min:[0,0],mesh_max:[200,200],probed_matrix:[[0,0],[0,0]],mesh_matrix:[[0,0],[0,0]],profiles:{default:saved,default1:saved}}}}}
  window.fetch=(url,opts={})=>String(url).includes('/api/mesh')?Promise.resolve(new Response(JSON.stringify(data),{headers:{'Content-Type':'application/json'}})):fetch(url,opts)
 })
 await page.goto(demo.url+'#bed')
 const profile=page.locator('select').filter({has:page.locator('option[value="active"]')})
 await profile.waitFor()
 await page.waitForFunction(()=>document.querySelector('option[value="active"]')?.textContent.includes('Mesh lato B'))
 assert.match(await profile.locator('option[value="active"]').innerText(), /Mesh lato B/)
 assert.equal(await profile.locator('option[value="default"]').innerText(),'Mesh lato A')
 assert.equal(await profile.locator('option[value="default1"]').innerText(),'Mesh lato B')
 await profile.selectOption('default')
 assert.equal(await profile.inputValue(),'default')
 await profile.selectOption('default1')
 assert.equal(await profile.inputValue(),'default1')
 assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),[])
 console.log('PASS: Italian mesh side labels, original profile values and no firmware command')
} finally {await browser.close();await demo.cleanup()}
