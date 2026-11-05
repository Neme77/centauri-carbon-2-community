import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture(`
window.__CC2_PREVIEW.preferences.language='en';
window.__CC2_PREVIEW.preferences.theme='obsidian';
const original=window.fetch;
window.fetch=async(input,options={})=>String(input)==='/api/cavity-fan-floor'
 ?new Response(JSON.stringify({installed:true,ready:true,printing:false,minimum:null}),{headers:{'Content-Type':'application/json'}})
 :original(input,options);
`)
const browser=await chromium.launch(launchOptions)
try{
 const page=await browser.newPage({viewport:{width:1440,height:1100}})
 await page.goto(demo.url+'#control')
 await page.locator('.cc2-camera-fan-management').waitFor()
 const check=async()=>page.evaluate(()=>{
  const rect=s=>document.querySelector(s).getBoundingClientRect()
  const card=rect('.cc2-fans'),title=rect('.cc2-fans>div:first-child'),first=rect('.cc2-fan-row')
  const rows=[...document.querySelectorAll('.cc2-fans .cc2-fan-row')].map(e=>e.getBoundingClientRect())
  const heading=rect('#cc2-camera-fan-title'),group=rect('.cc2-camera-fan-management')
  const flap=rect('.cc2-exhaust-grille button'),tuning=rect('.cc2-tuning')
  return {rows:rows.length,clearance:first.top-title.bottom,unit:first.height,
   afterFans:heading.top-rows[2].bottom,afterHeading:flap.top-heading.bottom,
   bottom:card.bottom-group.bottom,aligned:Math.abs(card.bottom-tuning.bottom),height:card.height}
 })
 const before=await check()
 assert.equal(before.rows,3)
 assert(before.clearance>20&&before.clearance<before.unit*1.5)
 assert(before.afterFans>30&&before.afterFans<100)
 assert(before.afterHeading>35&&before.afterHeading<100)
 assert(before.bottom>35&&before.aligned<2)
 assert.equal(await page.locator('.cc2-camera-fan-management .cc2-exhaust-grille').count(),1)
 assert((await page.locator('#cc2-camera-fan-title').textContent()).includes('Chamber fan management'))
 await page.goto(demo.url+'#dashboard')
 await page.locator('.cc2-dashboard-fans').waitFor()
 const gap=await page.locator('.cc2-dashboard-fans').evaluate(card=>{
  const header=card.children[0].getBoundingClientRect(),fan=card.children[1].getBoundingClientRect()
  return fan.top-header.bottom
 })
 assert(gap>=27&&gap<=29)
 for(const width of [390,768]){
  await page.setViewportSize({width,height:1000})
  for(const section of ['control','dashboard']){
   await page.goto(demo.url+'#'+section)
   await page.waitForFunction(()=>document.documentElement.scrollWidth<=innerWidth+1,{},{timeout:5000}).catch(async error=>{
    const outside=await page.evaluate(()=>[...document.querySelectorAll('main *')].filter(e=>e.getBoundingClientRect().right>innerWidth+1).slice(0,8).map(e=>({tag:e.tagName,class:e.className,right:e.getBoundingClientRect().right})))
    throw new Error(section+' '+width+'px overflow: '+JSON.stringify(outside),{cause:error})
   })
  }
 }
 console.log('PASS: fan row clearance, separate chamber controls, moderate bottom space, matched cards, dashboard one-element spacing and mobile width.')
}finally{await browser.close();await demo.cleanup()}
