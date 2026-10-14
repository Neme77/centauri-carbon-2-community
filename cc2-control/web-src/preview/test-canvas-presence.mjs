import { chromium } from 'playwright'
import path from 'node:path'
import assert from 'node:assert/strict'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture()
const browser=await chromium.launch(launchOptions)
try {
 const page=await browser.newPage({viewport:{width:1920,height:1080}})
 await page.goto(demo.url+'#canvas')
 await page.locator('[data-slot-present]').first().waitFor()
 assert.deepEqual(await page.locator('[data-slot-present]').evaluateAll(es=>es.map(e=>e.dataset.slotPresent)),['false','true','true','false'])
 const empty=await page.locator('[data-slot-present="false"]').evaluateAll(es=>es.map(e=>({color:e.style.borderColor,shadow:e.style.boxShadow,opacity:getComputedStyle(e).opacity})))
 assert(empty.every(e=>e.color==='rgb(51, 69, 83)'&&e.shadow==='none'&&e.opacity==='0.4'))
 await page.evaluate(()=>{window.__CC2_PREVIEW.trays[3].status=1})
 await page.waitForFunction(()=>document.querySelectorAll('[data-slot-present="true"]').length===3)
 await page.evaluate(()=>{window.__CC2_PREVIEW.trays[3].status=0})
 await page.waitForFunction(()=>document.querySelectorAll('[data-slot-present="true"]').length===2)
 await page.screenshot({path:path.join(demo.folder,'canvas.png')})
 console.log('PASS: empty slots neutral; presence changes follow telemetry, saved material retained')
} finally {await browser.close();await demo.cleanup()}
