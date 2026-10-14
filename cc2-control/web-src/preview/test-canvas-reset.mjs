import { chromium } from 'playwright'
import path from 'node:path'
import assert from 'node:assert/strict'
import { fixture, launchOptions } from './fixture.mjs'
const demo=await fixture()
const browser=await chromium.launch(launchOptions)
try {
 const page=await browser.newPage({viewport:{width:1920,height:1080}})
 await page.goto(demo.url+'#canvas')
 const reset=page.getByRole('button',{name:'Reimposta tutti i filamenti',exact:true})
 await reset.waitFor()
 await reset.click()
 assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),[])
 await page.getByRole('button',{name:'Annulla',exact:true}).click()
 assert.equal(await page.evaluate(()=>window.__CC2_PREVIEW.trays[3].filament_type),'PLA')
 await reset.click()
 await page.getByRole('button',{name:'Conferma',exact:true}).click()
 await page.waitForFunction(()=>document.querySelectorAll('[data-slot-present]')[1].style.borderColor==='rgb(51, 69, 83)')
 assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),['canvas:reset-all'])
 assert((await page.evaluate(()=>window.__CC2_PREVIEW.trays)).every(t=>!t.filament_type&&!t.filament_color))
 assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.trays.map(t=>t.status)),[0,1,1,0],'reset must not simulate unloading')
 await page.evaluate(()=>{window.__CC2_PREVIEW.status=2})
 await page.waitForFunction(()=>Array.from(document.querySelectorAll('button')).find(b=>b.textContent.includes('Reimposta tutti')).disabled)
 await page.evaluate(()=>{window.__CC2_PREVIEW.status=1;window.__CC2_PREVIEW.connected=false})
 await page.waitForTimeout(1800)
 assert.equal(await reset.isDisabled(),true)
 console.log('PASS reset all: confirmation, cancel, four slots, neutral unknown colours, no unloading, print/offline guards')
}finally{await browser.close();await demo.cleanup()}
