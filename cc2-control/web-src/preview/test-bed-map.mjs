import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
import { layoutObjects, onBed } from '../src/lib/object-layout.ts'

assert.equal(onBed([0, 256]), true)
for (const p of [null, [-1, 10], [257, 10], [NaN, 0], [0, Infinity]]) assert.equal(onBed(p), false)
const shape = [[30, 30], [70, 30], [70, 70], [30, 70]]
assert.deepEqual(layoutObjects([{name:'P',polygon:JSON.stringify(shape)}])[0].polygon,shape)
for (const polygon of ['bad', [[0,0],[1,1]], [[0,0],[1,1],[-1,1]], Array(513).fill([1,1])]) {
  assert.equal(layoutObjects([{name:'P',polygon}])[0].polygon,null)
}
const demo = await fixture()
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []; page.on('pageerror', e => errors.push(e.message))
  await page.addInitScript(() => localStorage.setItem('cc2-fixture-preferences','{"language":"en"}'))
  await page.goto(demo.url+'#job')
  await page.evaluate(() => {
    const state=window.__CC2_PREVIEW
    Object.assign(state, {status:2, filename:'copies.gcode', current:'Buddha.stl_id_2_copy_0', excluded:[], objectRequests:0})
    state.objectRows=[
      {name:'Buddha.stl_id_3_copy_0',center:[200,200]},
      {name:'Buddha.stl_id_0_copy_0',center:'[200,50]'},
      {name:'Buddha.stl_id_2_copy_0',center:[50,200]},
      {name:'Buddha.stl_id_1_copy_0',polygon:[[30,30],[70,30],[70,70],[30,70]]},
      {name:'Unknown <img src=x onerror=alert(1)>'},
      {name:'Outside',center:[300,10]},
    ]
    const original=window.fetch
    window.fetch=(url,opts={})=>{
      if(String(url).includes('/api/printer')) return original(url,opts).then(async r=>{
        const d=await r.json();d.print.filename=state.filename
        return new Response(JSON.stringify(d),{headers:{'Content-Type':'application/json'}})
      })
      if(String(url).includes('/api/exclude-objects')){
        state.objectRequests++
        return Promise.resolve(new Response(JSON.stringify({result:{status:{exclude_object:{objects:state.objectRows,current_object:state.current,excluded_objects:state.excluded}}}}),{headers:{'Content-Type':'application/json'}}))
      }
      if(String(url).includes('/api/control')&&String(opts.body).startsWith('object:exclude:')){
        state.commands.push(opts.body);state.excluded.push(String(opts.body).slice('object:exclude:'.length))
        return Promise.resolve(new Response('{"accepted":true}',{headers:{'Content-Type':'application/json'}}))
      }
      return original(url,opts)
    }
  })
  const open=page.getByRole('button',{name:'Expand bed map',exact:true})
  await open.waitFor();await page.waitForFunction(()=>!Array.from(document.querySelectorAll('button')).find(e=>e.textContent.trim()==='Expand bed map').disabled)
  assert.equal(await page.locator('[data-bed-map]').count(),1,'one map is already visible in Job')
  assert.equal(await page.locator('[data-object-id]').count(),6,'only one compact object list')
  const inlineMap=await page.locator('[data-bed-map]').boundingBox()
  await page.locator('[data-bed-object-id="Buddha.stl_id_1_copy_0"]').click()
  assert.match(await page.locator('[data-bed-selection]').innerText(),/X 50\.0 \/ Y 50\.0 mm/)
  for(const [theme,width] of [['dark',1440],['light',1440],['dark',390]]) {
    await page.evaluate(theme=>document.documentElement.dataset.theme=theme,theme)
    await page.setViewportSize({width,height:1000});await page.waitForTimeout(300)
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'inline map overflow')
    const square=await page.locator('[data-bed-map]').boundingBox()
    assert(Math.abs(square.width-square.height)<1,`bed must stay square at ${width}: ${JSON.stringify(square)}`)
    if(process.env.CC2_AXE_PATH){
      await page.addScriptTag({path:process.env.CC2_AXE_PATH})
      const violations=await page.evaluate(async()=> (await axe.run(document.querySelector('.cc2-object-map'))).violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})))
      assert.deepEqual(violations,[],`inline accessibility ${theme}/${width}`)
    }
    if(process.env.CC2_SCREENSHOTS){
      await mkdir(process.env.CC2_SCREENSHOTS,{recursive:true})
      await page.screenshot({path:path.join(process.env.CC2_SCREENSHOTS,`bed-map-inline-${theme}-${width}.png`),fullPage:true})
    }
  }
  await page.setViewportSize({width:1440,height:1000});await page.waitForTimeout(300)
  await open.click()
  const dialog=page.getByRole('dialog')
  await dialog.getByRole('heading',{name:'Bed map',exact:true}).waitFor()
  const markers=dialog.locator('[data-bed-object-id]')
  assert.equal(await page.locator('[data-bed-map]').count(),1,'expansion replaces the inline map')
  assert((await dialog.locator('[data-bed-map]').boundingBox()).width>inlineMap.width,'expanded map is larger')
  assert.equal(await markers.count(),4,'unknown/out-of-bed positions are not invented or clipped onto the bed')
  assert.equal(await dialog.locator('[data-bed-list-id]').count(),6)
  assert.equal(await dialog.locator('img').count(),0,'object names are escaped')
  await dialog.getByText('2 objects have no usable bed position. Select them from the list.',{exact:true}).waitFor()
  assert.equal(await dialog.locator('[data-bed-map] polygon').count(),1)
  const frontLeft=dialog.locator('[data-bed-object-id="Buddha.stl_id_1_copy_0"]')
  const frontRight=dialog.locator('[data-bed-object-id="Buddha.stl_id_0_copy_0"]')
  const backLeft=dialog.locator('[data-bed-object-id="Buddha.stl_id_2_copy_0"]')
  const [fl,fr,bl]=await Promise.all([frontLeft.boundingBox(),frontRight.boundingBox(),backLeft.boundingBox()])
  assert(fl.x<fr.x&&fl.y>bl.y,'X grows right; Y grows away from the front')
  const fraction=await frontLeft.evaluate(el=>({left:parseFloat(el.style.left)/100,bottom:parseFloat(el.style.bottom)/100}))
  assert(Math.abs(fraction.left-50/256)<1e-6&&Math.abs(fraction.bottom-50/256)<1e-6)
  await frontRight.click()
  assert.equal(await frontRight.getAttribute('aria-pressed'),'true')
  assert.equal(await backLeft.getAttribute('data-current'),'true')
  await page.evaluate(()=>{window.__CC2_PREVIEW.objectRows.reverse();window.__CC2_PREVIEW.current='Buddha.stl_id_3_copy_0'})
  await page.waitForFunction(()=>document.querySelector('[data-bed-object-id="Buddha.stl_id_3_copy_0"]')?.getAttribute('data-current')==='true')
  assert.equal(await frontRight.getAttribute('aria-pressed'),'true','printing highlight must not steal manual selection')
  assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),[],'selection sends no commands')
  await dialog.getByRole('button',{name:'Exclude Selected Object',exact:true}).click()
  let confirm=page.getByRole('dialog').last()
  await confirm.getByRole('button',{name:'Cancel',exact:true}).click()
  assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),[])
  await dialog.getByRole('button',{name:'Exclude Selected Object',exact:true}).click()
  confirm=page.getByRole('dialog').last()
  await confirm.getByRole('button',{name:'Confirm',exact:true}).click()
  await page.waitForFunction(()=>window.__CC2_PREVIEW.commands.length===1)
  assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),['object:exclude:Buddha.stl_id_0_copy_0'])
  await page.waitForFunction(()=>document.querySelector('[data-bed-object-id="Buddha.stl_id_0_copy_0"]')?.disabled)
  const listFirst=dialog.locator('[data-bed-list-id="Buddha.stl_id_1_copy_0"]')
  await listFirst.focus();await page.keyboard.press('Enter')
  assert.equal(await frontLeft.getAttribute('aria-pressed'),'true','list and map share selection')
  await page.evaluate(()=>window.__CC2_PREVIEW.objectRows=window.__CC2_PREVIEW.objectRows.filter(o=>o.name.startsWith('Buddha')))
  await page.waitForFunction(()=>document.querySelectorAll('[data-bed-list-id]').length===4)
  for (const [theme,width] of [['dark',1440],['light',1440],['dark',390],['light',768]]) {
    await page.evaluate(theme=>document.documentElement.dataset.theme=theme,theme)
    await page.setViewportSize({width,height:900})
    await page.waitForTimeout(300)
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'page overflow')
    assert(await dialog.evaluate(el=>el.scrollWidth<=el.clientWidth),'dialog overflow')
    const rect=await dialog.boundingBox();assert(rect.x>=0&&rect.x+rect.width<=width,'dialog must stay inside viewport')
    const square=await dialog.locator('[data-bed-map]').boundingBox()
    assert(Math.abs(square.width-square.height)<1,'expanded bed stays square')
    if(process.env.CC2_AXE_PATH){
      await page.addScriptTag({path:process.env.CC2_AXE_PATH})
      const violations=await page.evaluate(async()=> (await axe.run(document.querySelector('[role=dialog]'))).violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})))
      assert.deepEqual(violations,[],`expanded accessibility ${theme}/${width}`)
    }
    if(process.env.CC2_SCREENSHOTS){
      await mkdir(process.env.CC2_SCREENSHOTS,{recursive:true})
      await page.screenshot({path:path.join(process.env.CC2_SCREENSHOTS,`bed-map-${theme}-${width}.png`)})
    }
  }
  await page.keyboard.press('Escape');assert.equal(await page.getByRole('dialog').count(),0)
  assert.equal(await page.locator('[data-bed-map]').count(),1,'inline map restored on close')
  assert.equal(await open.evaluate(el=>document.activeElement===el),true,'focus returns to map button')
  await open.focus();await page.keyboard.press('Enter');await dialog.waitFor()
  await dialog.getByRole('button',{name:'Close bed map',exact:true}).click()
  // Many copies and both bed corners keep their original command IDs and remain selectable.
  await page.evaluate(()=>{
    window.__CC2_PREVIEW.excluded=[]
    window.__CC2_PREVIEW.objectRows=Array.from({length:40},(_,i)=>({name:`Copies.stl_id_${i}_copy_0`,center:[16+(i%8)*32,16+Math.floor(i/8)*48]})).concat([{name:'Front corner',center:[0,0]},{name:'Back corner',center:[256,256]}])
  })
  await page.waitForFunction(()=>document.querySelectorAll('[data-object-id]').length===42)
  await open.click()
  assert.equal(await markers.count(),42)
  await dialog.locator('[data-bed-object-id="Back corner"]').focus();await page.keyboard.press('Enter')
  assert.equal(await dialog.locator('[data-bed-list-id="Back corner"]').getAttribute('aria-pressed'),'true')
  await dialog.getByRole('button',{name:'Close bed map',exact:true}).click()
  // Complete coordinate loss still leaves the list available without false map markers.
  await page.evaluate(()=>window.__CC2_PREVIEW.objectRows=[{name:'Overlap A',center:[100,100]},{name:'Overlap B',center:[100,100]}])
  await page.waitForFunction(()=>document.querySelectorAll('[data-object-id]').length===2)
  await open.click()
  await dialog.locator('[data-bed-list-id="Overlap B"]').click()
  assert.equal(await dialog.locator('[data-bed-object-id="Overlap B"]').getAttribute('aria-pressed'),'true')
  await dialog.locator('[data-bed-list-id="Overlap A"]').focus();await page.keyboard.press('Enter')
  assert.equal(await dialog.locator('[data-bed-object-id="Overlap A"]').getAttribute('aria-pressed'),'true','coincident markers stay selectable from the compact list')
  await dialog.getByRole('button',{name:'Close bed map',exact:true}).click()
  // Coordinate loss still leaves the compact list available.
  await page.evaluate(()=>window.__CC2_PREVIEW.objectRows=[{name:'No position'}])
  await page.waitForFunction(()=>document.querySelector('[data-object-id]')?.dataset.objectId==='No position')
  await open.click();assert.equal(await markers.count(),0)
  await dialog.locator('[data-bed-list-id]').click()
  assert.equal(await dialog.getByRole('button',{name:'Exclude Selected Object',exact:true}).isEnabled(),true)
  await page.evaluate(()=>window.__CC2_PREVIEW.filename='different.gcode')
  await page.waitForFunction(()=>document.querySelectorAll('[role=dialog]').length===0)
  await open.click();await dialog.waitFor()
  await page.evaluate(()=>window.__CC2_PREVIEW.status=1)
  await page.waitForFunction(()=>document.querySelectorAll('[role=dialog]').length===0)
  assert.equal(await open.isEnabled(),false)
  const count=await page.evaluate(()=>window.__CC2_PREVIEW.objectRequests)
  await page.waitForTimeout(2500)
  assert.equal(await page.evaluate(()=>window.__CC2_PREVIEW.objectRequests),count,'idle does not poll for objects')
  assert.deepEqual(errors,[])
  console.log('PASS: physical map positions, polygons, live highlight without auto-selection, original IDs, confirmation/cancel, keyboard, themes, mobile, unknown positions, job change and idle cleanup')
} finally {await browser.close();await demo.cleanup()}
