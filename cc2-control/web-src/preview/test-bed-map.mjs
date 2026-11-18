import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
import { layoutObjects, onBed } from '../src/lib/object-layout.ts'
import { splitLayerPaths } from '../src/lib/layer-path-geom.ts'

assert.equal(onBed([0, 256]), true)
assert.deepEqual(splitLayerPaths([[[0, 0], [10, 0], [20, 0]]], [10, 0], 'split'), {
  printed: [[[0, 0], [10, 0]]],
  remaining: [[[10, 0], [20, 0]]],
})
assert.deepEqual(splitLayerPaths([[[0, 0], [10, 0], [20, 0]]], [5, 1], 'split'), {
  printed: [[[0, 0], [5, 0]]],
  remaining: [[[5, 0], [10, 0], [20, 0]]],
})
assert.deepEqual(splitLayerPaths([[[0, 0], [10, 0]]], null, 'printed').remaining, [])
assert.equal(splitLayerPaths([[[0, 0], [10, 0]]], [0, 0], 'remaining').printed.length, 0)
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
    Object.assign(state, {status:2, filename:'copies.gcode', current:'Buddha.stl_id_2_copy_0', excluded:[], objectRequests:0, layer:143, totalLayers:227, layerPathFail:false, layerPathDelay:0})
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
        d.print.current_layer=state.layer;d.print.total_layers=state.totalLayers;d.print.duration=600
        d.motion={...d.motion,x:128,y:128,z:28.6}
        return new Response(JSON.stringify(d),{headers:{'Content-Type':'application/json'}})
      })
      if(String(url).includes('/api/gcode-files/layer-path')){
        if(state.skipFixture){
          const layer=Number(new URL(String(url),location.href).searchParams.get('layer'))
          return Promise.resolve(new Response(JSON.stringify({storage:'internal',file:state.filename,layer,total_layers:state.totalLayers,z:layer*0.2,paths:[[[35,35],[65,35],[65,65]],[[100,100],[150,100]]],point_count:5,truncated:false}),{headers:{'Content-Type':'application/json'}}))
        }
        if(state.layerPathFail) return Promise.resolve(new Response(JSON.stringify({error:'not found'}),{status:404,headers:{'Content-Type':'application/json'}}))
        if(state.layerPathDelay) return new Promise(resolve=>setTimeout(()=>resolve(original(url,opts)),state.layerPathDelay))
        return original(url,opts)
      }
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
  // The Job page is one card: head, the print bar, one divider, then the bed map with its Objects / Layer path tabs.
  const card=page.locator('section:has(.cc2-job-bar)')
  await page.waitForFunction(()=>document.querySelectorAll('[data-bed-object-id]').length>0)
  assert.equal(await page.locator('.cc2-job-bar').count(),1)
  assert.equal(await card.count(),1,'one card holds the job and the bed map')
  await card.getByRole('heading',{name:'Current Job',exact:true}).waitFor()
  await card.getByRole('heading',{name:'Object Exclusion',exact:true}).waitFor()
  assert.equal(await page.getByRole('button',{name:'Expand bed map'}).count(),0,'no expand button: the map is on the page')
  assert.equal(await page.getByRole('button',{name:'Open in new window'}).count(),0,'no camera card on the Job page')
  assert.equal(await card.locator('.cc2-card-head').count(),0,'no card-head divider inside the card')
  assert.equal(await card.locator('div.border-t').count(),1,'exactly one divider, between the job and the objects')
  assert.equal(await page.locator('[data-bed-map]').count(),1,'one map is on the Job page')
  assert.equal(await page.locator('[data-object-id]').count(),6,'only one object list')
  // The print bar keeps every figure of the compact bar, including the estimated total time.
  for(const text of ['Current layer','Elapsed','Est. Total Print Time','Nozzle','Heated Bed','Chamber']) await card.locator('.cc2-job-bar').getByText(text,{exact:false}).first().waitFor()
  // Camera: one button shows the picture alone, starts the stream when it opens and ends it when it closes.
  const camButton=card.getByRole('button',{name:'Camera',exact:true})
  await page.evaluate(()=>{window.__claims=0;const f=window.fetch;window.fetch=(u,o)=>{if(String(u).includes('/api/camera/claim'))window.__claims++;return f(u,o)}})
  const barSpots=()=>page.evaluate(()=>[...document.querySelectorAll('.cc2-job-bar button, .cc2-job-bar strong')].filter(e=>!e.closest('.cc2-job-camera')).map(e=>{const r=e.getBoundingClientRect();return Math.round(r.left)+':'+Math.round(r.top+scrollY)}).join(' '))
  const camera=page.locator('.cc2-job-camera')
  assert.equal(await camButton.getAttribute('aria-pressed'),'false')
  assert.equal(await camera.count(),0,'no picture while the camera is closed')
  assert.equal(await page.evaluate(()=>window.__claims),0,'no stream while the camera is closed')
  let openings=0
  for(const width of [1440,390]){
    await page.setViewportSize({width,height:1000});await page.waitForTimeout(400)
    const closed=await barSpots()
    openings++
    await camButton.click();await camera.locator('.cc2-camera-frame').waitFor()
    assert.equal(await camButton.getAttribute('aria-pressed'),'true')
    // (The preview has no camera, so the frame shows its retry note; a real picture shows nothing but the image.)
    assert.equal(await camera.locator('h2').count(),0,'the picture has no title')
    assert.equal(await camera.getByRole('button',{name:/Pause|Start live view|Open in new window|Snapshot/}).count(),0,'the picture has no extra buttons')
    // The stream starts right after the picture is on the page, not in the same instant: wait for it, then make sure it stays one.
    await page.waitForFunction(n=>window.__claims>=n,openings,{timeout:5000});await page.waitForTimeout(200)
    assert.equal(await page.evaluate(()=>window.__claims),openings,'one stream start per opening')
    assert.equal(await card.getByRole('button',{name:'Open in new window'}).count()+await card.getByRole('button',{name:'Snapshot'}).count()+await card.getByRole('button',{name:'Start live view'}).count(),0)
    assert.equal(await barSpots(),closed,`buttons and figures stay put when the camera opens at ${width}px`)
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`no sideways scroll with the camera at ${width}px`)
    assert.equal(await page.evaluate(()=>localStorage.getItem('cc2-job-camera')),'1')
    await camButton.click();await camera.waitFor({state:'detached'})
    assert.equal(await barSpots(),closed,`buttons and figures stay put when the camera closes at ${width}px`)
    assert.equal(await page.evaluate(()=>localStorage.getItem('cc2-job-camera')),'0')
  }
  await page.setViewportSize({width:1440,height:1000})
  assert.equal(await page.evaluate(()=>window.__claims),2,'none after closing')
  // Hidden tab: the stream pauses with a quiet note and no button; it comes back by itself when the page is shown again.
  await camButton.click();await camera.locator('.cc2-camera-frame').waitFor()
  const visibility=hidden=>page.evaluate(hidden=>{Object.defineProperty(document,'hidden',{configurable:true,get:()=>hidden});document.dispatchEvent(new Event('visibilitychange'))},hidden)
  await page.waitForFunction(()=>window.__claims===3)
  await visibility(true);await camera.getByText('Live view paused while this browser tab was hidden').waitFor()
  assert.equal(await camera.locator('button').count(),0,'a hidden page leaves a quiet note and no start button')
  await visibility(false);await page.waitForFunction(()=>window.__claims===4)
  await camera.getByText('Live view paused while this browser tab was hidden').waitFor({state:'detached'})
  assert.equal(await camera.getByRole('button',{name:'Start live view'}).count(),0,'the stream restarts by itself, without a start button')
  await camButton.click();await camera.waitFor({state:'detached'})
  // Opened while the tab is hidden (the page remembers the open camera): no stream yet, it starts once the page is shown.
  await visibility(true)
  await camButton.click();await camera.locator('.cc2-camera-frame').waitFor()
  await page.waitForTimeout(200)
  assert.equal(await page.evaluate(()=>window.__claims),4,'no stream while the page is hidden')
  await visibility(false);await page.waitForFunction(()=>window.__claims===5,null,{timeout:5000})
  await camButton.click();await camera.waitFor({state:'detached'})
  // A refused camera leaves a button to try again.
  await page.evaluate(()=>{window.__refuse=true;const f=window.fetch;window.fetch=(u,o)=>window.__refuse&&String(u).includes('/api/camera/claim')?Promise.resolve(new Response('{"viewer":"another-viewer"}',{headers:{'Content-Type':'application/json'}})):f(u,o)})
  await camButton.click()
  const again=camera.getByRole('button',{name:'Start live view',exact:true})
  await again.waitFor({timeout:5000})
  await page.evaluate(()=>{window.__refuse=false})
  await again.click();await page.waitForFunction(()=>window.__claims===6,null,{timeout:5000})
  await camButton.click();await camera.waitFor({state:'detached'})
  await page.evaluate(()=>Object.defineProperty(document,'hidden',{configurable:true,get:()=>false}))
  for(const name of ['Pause','Resume','Cancel']) await card.getByRole('button',{name,exact:true}).waitFor()
  await page.locator('[data-bed-object-id="Buddha.stl_id_1_copy_0"]').click()
  assert.match(await page.locator('[data-bed-selection]').innerText(),/X 50\.0 \/ Y 50\.0 mm/)
  for(const [theme,width] of [['dark',1440],['light',1440],['dark',390]]) {
    await page.evaluate(theme=>document.documentElement.dataset.theme=theme,theme)
    await page.setViewportSize({width,height:1000});await page.waitForTimeout(300)
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'map overflow')
    const square=await page.locator('[data-bed-map]').boundingBox()
    assert(Math.abs(square.width-square.height)<1,`bed must stay square at ${width}: ${JSON.stringify(square)}`)
    if(process.env.CC2_AXE_PATH){
      await page.addScriptTag({path:process.env.CC2_AXE_PATH})
      const violations=await page.evaluate(async()=> (await axe.run(document.querySelector('.cc2-object-map'))).violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})))
      assert.deepEqual(violations,[],`accessibility ${theme}/${width}`)
    }
    if(process.env.CC2_SCREENSHOTS){
      await mkdir(process.env.CC2_SCREENSHOTS,{recursive:true})
      await page.screenshot({path:path.join(process.env.CC2_SCREENSHOTS,`job-${theme}-${width}.png`),fullPage:true})
    }
  }
  await page.setViewportSize({width:1440,height:1000});await page.waitForTimeout(300)
  const dialog=card
  const markers=dialog.locator('[data-bed-object-id]')
  // The legend sits in the top row next to the size, above the map.
  const sizeRow=await dialog.getByText('256 × 256 mm',{exact:true}).boundingBox()
  const legendBox=await dialog.locator('.cc2-object-map-legend').boundingBox()
  const mapBox=await dialog.locator('[data-bed-map]').boundingBox()
  assert.equal(await dialog.locator('.cc2-object-map-legend').count(),1)
  assert(Math.abs(legendBox.y-sizeRow.y)<8&&legendBox.y+legendBox.height<=mapBox.y,'the legend is in the top row, above the map')
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
  assert.equal(await dialog.locator('[data-bed-map-tab]').getAttribute('data-bed-map-tab'),'objects')
  assert.equal(await page.locator('[data-bed-map-tab]').count(),1)
  // Objects tab during a print: Live can be pressed (it switches to the Layer path and follows it), but it is not lit;
  // the layer arrows do not apply, so they stay in place but are disabled.
  const live=dialog.locator('[data-layer-live]')
  const arrows=['Previous layer','Next layer'].map(name=>dialog.getByRole('button',{name}))
  assert.equal(await live.isEnabled(),true,'Live can be pressed on the Objects tab during a print')
  assert.equal(await live.getAttribute('aria-pressed'),'false','Live does not light up on the Objects tab')
  for(const arrow of arrows) assert.equal(await arrow.isDisabled(),true,'layer arrows are disabled on the Objects tab')
  const objectsBoxes=await Promise.all([live,...arrows].map(b=>b.boundingBox()))
  // The whole object area is the click target, not only its number.
  const outline=dialog.locator('[data-bed-outline-id="Buddha.stl_id_1_copy_0"]')
  await frontRight.click()
  assert.equal(await frontRight.getAttribute('aria-pressed'),'true')
  assert.equal(await frontLeft.getAttribute('aria-pressed'),'false')
  await outline.click({position:{x:4,y:4}})
  assert.equal(await frontLeft.getAttribute('aria-pressed'),'true','a click on the object area selects it')
  assert.equal(await frontRight.getAttribute('aria-pressed'),'false')
  assert.equal(await outline.getAttribute('data-selected'),'true','the Objects tab keeps its status colours')
  assert.match(await dialog.locator('.cc2-object-map-legend').innerText(),/Selected[\s\S]*Current Object[\s\S]*Excluded/,'the Objects legend names the status colours')
  const objectsBadge=await frontLeft.boundingBox()
  assert(Math.abs(objectsBadge.x+objectsBadge.width/2-(await outline.boundingBox()).x-(await outline.boundingBox()).width/2)<2,'on the Objects tab the number sits in the middle of the object')
  // The look of the numbers on the Objects tab: the Layer path tab keeps it and only moves the number beside the object.
  const markerLook=el=>el.evaluate(e=>{const s=getComputedStyle(e),r=e.getBoundingClientRect();return {w:Math.round(r.width),h:Math.round(r.height),bg:s.backgroundColor,color:s.color,border:s.borderTopColor+' '+s.borderTopWidth,radius:s.borderTopLeftRadius,size:s.fontSize,weight:s.fontWeight,opacity:s.opacity,outline:s.outlineStyle+' '+s.outlineColor+' '+s.outlineWidth,shadow:s.boxShadow}})
  const objectsLook={selected:await markerLook(frontLeft),current:await markerLook(backLeft),plain:await markerLook(frontRight)}
  assert.deepEqual([objectsLook.plain.w,objectsLook.plain.h],[32,32],'the number is 32 x 32 px on the Objects tab')
  await page.evaluate(()=>window.__CC2_PREVIEW.layerPathDelay=400)
  await dialog.getByRole('button',{name:'Layer path',exact:true}).click()
  await dialog.getByText('Loading layer…',{exact:true}).first().waitFor()
  await page.waitForFunction(()=>document.querySelectorAll('[data-layer-path]').length>0)
  await page.evaluate(()=>window.__CC2_PREVIEW.layerPathDelay=0)
  assert.match(await dialog.locator('[data-layer-status]').innerText(),/Layer 143 \/ 227/)
  assert.ok(await dialog.locator('[data-layer-nozzle]').count()>=1)
  assert.equal(await dialog.locator('[data-layer-nozzle]').getAttribute('aria-label'),'Nozzle')
  assert.ok(await dialog.locator('[data-layer-path="printed"]').count(),'printed path up to the nozzle')
  assert.ok(await dialog.locator('[data-layer-path="remain"]').count(),'remaining path after the nozzle')
  // The previous layer is requested after the current one has been drawn.
  await page.waitForFunction(()=>document.querySelectorAll('[data-layer-path="prev"]').length>0)
  assert.equal(await live.isEnabled(),true,'Live works on the Layer path tab')
  const layerBoxes=await Promise.all([live,...arrows].map(x=>x.boundingBox()))
  layerBoxes.forEach((box,i)=>assert(Math.abs(box.x-objectsBoxes[i].x)<1&&Math.abs(box.y-objectsBoxes[i].y)<1,'switching the tab moves no control'))
  const badge=await frontLeft.boundingBox(),shape=await outline.boundingBox()
  assert(badge.x+badge.width<=shape.x+0.5,'on the Layer path tab the number sits left of the object, outside its outline')
  assert(Math.abs(badge.y+badge.height/2-(shape.y+shape.height/2))<2,'the number is level with the middle of the object')
  // Same look as on the Objects tab: size, colours, border, outline of the current object; only the place differs.
  assert.deepEqual(await markerLook(frontLeft),objectsLook.selected,'a selected number looks the same on both tabs')
  assert.deepEqual(await markerLook(backLeft),objectsLook.current,'the current number looks the same on both tabs')
  assert.deepEqual(await markerLook(frontRight),objectsLook.plain,'a plain number looks the same on both tabs')
  assert.equal(await frontLeft.getAttribute('data-layer-badge'),'true','the number is marked as the layer path badge')
  assert.equal(await frontLeft.evaluate(el=>getComputedStyle(el).transform!=='none'),true)
  assert.equal(await dialog.locator('[data-bed-map] polygon').count(),1,'layer overlay uses polylines, not extra polygons')
  // Layer path tab: the objects keep their status marks (selected, current), on the number and on the outline.
  assert.equal(await outline.getAttribute('data-selected'),'true','the selected outline is marked on the Layer path tab')
  assert.equal(await frontLeft.getAttribute('data-selected'),'true','the selected number is marked on the Layer path tab')
  assert.equal(await backLeft.getAttribute('data-current'),'true','the current number is marked on the Layer path tab')
  assert.equal(await outline.getAttribute('data-current'),null);assert.equal(await backLeft.getAttribute('data-selected'),null)
  for(const el of [outline,backLeft]) assert.equal(await el.getAttribute('data-excluded'),null,'nothing is excluded yet')
  const legendText=await dialog.locator('.cc2-object-map-legend').innerText()
  assert.match(legendText,/Selected[\s\S]*Current Object[\s\S]*Excluded/,'the Layer path legend also names the status colours')
  assert.match(legendText,/Layer path/);assert.match(legendText,/Nozzle/)
  // The path of an excluded object is not printed: pale grey, not the path colour.
  await page.evaluate(()=>{window.__CC2_PREVIEW.skipFixture=true;window.__CC2_PREVIEW.excluded=['Buddha.stl_id_1_copy_0']})
  await page.waitForFunction(()=>document.querySelector('[data-bed-object-id="Buddha.stl_id_1_copy_0"]')?.disabled)
  // An excluded object is marked on the Layer path tab too: number and outline, red and not selectable.
  assert.equal(await frontLeft.getAttribute('data-excluded'),'true','the excluded number is marked on the Layer path tab')
  assert.equal(await outline.getAttribute('data-excluded'),'true','the excluded outline is marked on the Layer path tab')
  assert.match(await dialog.locator('.cc2-object-map-legend').innerText(),/Excluded/)
  await dialog.getByRole('button',{name:'Previous layer'}).click()
  await page.waitForFunction(()=>document.querySelector('[data-layer-status]')?.textContent.includes('Layer 142'))
  // Layer 142 came from the cache; layer 141 is fetched with the mocked path.
  await dialog.getByRole('button',{name:'Previous layer'}).click()
  await page.waitForFunction(()=>document.querySelector('[data-layer-status]')?.textContent.includes('Layer 141'))
  await page.waitForFunction(()=>document.querySelectorAll('[data-layer-path="skipped"]').length===1)
  const stroke=sel=>dialog.locator(sel).first().evaluate(el=>getComputedStyle(el).stroke)
  assert.notEqual(await stroke('[data-layer-path="skipped"]'),await stroke('[data-layer-path="printed"], [data-layer-path="remain"]'),'the path of an excluded object is not drawn in the path colour')
  assert.equal(await dialog.locator('[data-layer-path="printed"], [data-layer-path="remain"]').count()>0,true,'the other object keeps its path')
  await page.evaluate(()=>{window.__CC2_PREVIEW.skipFixture=false;window.__CC2_PREVIEW.excluded=[]})
  await page.waitForFunction(()=>!document.querySelector('[data-bed-object-id="Buddha.stl_id_1_copy_0"]')?.disabled)
  await dialog.getByRole('button',{name:'Next layer'}).click()
  await page.waitForFunction(()=>document.querySelector('[data-layer-status]')?.textContent.includes('Layer 142'))
  assert.equal(await dialog.locator('[data-layer-live]').getAttribute('aria-pressed'),'false')
  await dialog.getByRole('button',{name:'Live',exact:true}).click()
  await page.waitForFunction(()=>document.querySelector('[data-layer-status]')?.textContent.includes('Layer 143'))
  assert.equal(await dialog.locator('[data-layer-live]').getAttribute('aria-pressed'),'true')
  await dialog.getByRole('button',{name:'Next layer'}).click()
  await page.waitForFunction(()=>document.querySelector('[data-layer-status]')?.textContent.includes('Layer 144'))
  await page.evaluate(()=>window.__CC2_PREVIEW.layerPathFail=true)
  await dialog.getByRole('button',{name:'Next layer'}).click()
  await dialog.getByText('Layer path not available',{exact:true}).first().waitFor()
  assert.equal(await dialog.locator('[data-bed-map] polygon').count(),1,'unavailable path falls back to the objects map')
  await page.evaluate(()=>window.__CC2_PREVIEW.layerPathFail=false)
  await dialog.getByRole('button',{name:'Live',exact:true}).click()
  await page.waitForFunction(()=>document.querySelectorAll('[data-layer-path]').length>0)
  await dialog.getByRole('button',{name:'Objects',exact:true}).click()
  assert.equal(await dialog.locator('[data-bed-map-tab]').getAttribute('data-bed-map-tab'),'objects')
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
  await dialog.getByRole('button',{name:'Layer path',exact:true}).click()
  await page.waitForFunction(()=>document.querySelectorAll('[data-layer-path]').length>0)
  for (const [theme,width] of [['dark',1440],['light',1440],['obsidian',1440],['nord',1440],['solarized-light',1440],['dark',390],['nord',390],['solarized-light',390],['light',768]]) {
    await page.evaluate(theme=>document.documentElement.dataset.theme=theme,theme)
    await page.setViewportSize({width,height:900})
    await page.waitForTimeout(300)
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'page overflow')
    assert(await dialog.evaluate(el=>el.scrollWidth<=el.clientWidth),'card overflow')
    const rect=await dialog.boundingBox();assert(rect.x>=0&&rect.x+rect.width<=width,'the card stays inside the viewport')
    const square=await dialog.locator('[data-bed-map]').boundingBox()
    assert(Math.abs(square.width-square.height)<1,'the bed stays square')
    if(process.env.CC2_AXE_PATH){
      await page.addScriptTag({path:process.env.CC2_AXE_PATH})
      const violations=await page.evaluate(async()=> (await axe.run(document.querySelector('.cc2-job-bar').closest('section'))).violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})))
      assert.deepEqual(violations,[],`layer path accessibility ${theme}/${width}`)
    }
    if(process.env.CC2_SCREENSHOTS){
      await mkdir(process.env.CC2_SCREENSHOTS,{recursive:true})
      await page.screenshot({path:path.join(process.env.CC2_SCREENSHOTS,`bed-map-${theme}-${width}.png`)})
      await page.screenshot({path:path.join(process.env.CC2_SCREENSHOTS,`bed-map-layer-${theme}-${width}.png`)})
    }
  }
  // Switching the tab moves nothing: tabs, arrows, Live and the map stay where they are (1440 and 390 px).
  for(const width of [1440,390]){
    await page.setViewportSize({width,height:1000});await page.waitForTimeout(400)
    await dialog.getByRole('button',{name:'Objects',exact:true}).click();await page.waitForTimeout(300)
    const spot=()=>page.evaluate(()=>[...document.querySelectorAll('.cc2-job-bar, fieldset button, [data-layer-live], [data-bed-map], [data-layer-status]')].map(e=>Math.round(e.getBoundingClientRect().top+scrollY)))
    const onObjects=await spot()
    await dialog.getByRole('button',{name:'Layer path',exact:true}).click();await page.waitForTimeout(800)
    const onLayer=await spot()
    onLayer.forEach((y,i)=>assert(Math.abs(y-onObjects[i])<=1,`switching the tab moves nothing at ${width}px: ${onObjects} -> ${onLayer}`))
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`no sideways scroll at ${width}px`)
  }
  await dialog.getByRole('button',{name:'Objects',exact:true}).click()
  await page.setViewportSize({width:1440,height:1000})
  // Many copies and both bed corners keep their original command IDs and remain selectable.
  await page.evaluate(()=>{
    window.__CC2_PREVIEW.excluded=[]
    window.__CC2_PREVIEW.objectRows=Array.from({length:40},(_,i)=>({name:`Copies.stl_id_${i}_copy_0`,center:[16+(i%8)*32,16+Math.floor(i/8)*48]})).concat([{name:'Front corner',center:[0,0]},{name:'Back corner',center:[256,256]}])
  })
  await page.waitForFunction(()=>document.querySelectorAll('[data-object-id]').length===42)
  assert.equal(await markers.count(),42)
  await dialog.locator('[data-bed-object-id="Back corner"]').focus();await page.keyboard.press('Enter')
  assert.equal(await dialog.locator('[data-bed-list-id="Back corner"]').getAttribute('aria-pressed'),'true')
  // Complete coordinate loss still leaves the list available without false map markers.
  await page.evaluate(()=>window.__CC2_PREVIEW.objectRows=[{name:'Overlap A',center:[100,100]},{name:'Overlap B',center:[100,100]}])
  await page.waitForFunction(()=>document.querySelectorAll('[data-object-id]').length===2)
  await dialog.locator('[data-bed-list-id="Overlap B"]').click()
  assert.equal(await dialog.locator('[data-bed-object-id="Overlap B"]').getAttribute('aria-pressed'),'true')
  await dialog.locator('[data-bed-list-id="Overlap A"]').focus();await page.keyboard.press('Enter')
  assert.equal(await dialog.locator('[data-bed-object-id="Overlap A"]').getAttribute('aria-pressed'),'true','coincident markers stay selectable from the compact list')
  // Coordinate loss still leaves the compact list available.
  await page.evaluate(()=>window.__CC2_PREVIEW.objectRows=[{name:'No position'}])
  await page.waitForFunction(()=>document.querySelector('[data-object-id]')?.dataset.objectId==='No position')
  assert.equal(await markers.count(),0)
  await dialog.locator('[data-bed-list-id]').click()
  assert.equal(await dialog.getByRole('button',{name:'Exclude Selected Object',exact:true}).isEnabled(),true)
  // No print: the tabs are greyed out, the status says so, nothing leaves the page.
  await page.evaluate(()=>window.__CC2_PREVIEW.status=1)
  await page.waitForFunction(()=>[...document.querySelectorAll('fieldset button')].every(b=>b.disabled))
  assert.match(await dialog.locator('[data-layer-status]').innerText(),/No print/)
  assert.equal(await dialog.getByRole('button',{name:'Layer path',exact:true}).isDisabled(),true)
  assert.equal(await live.isDisabled(),true,'Live is locked without a print')
  const count=await page.evaluate(()=>window.__CC2_PREVIEW.objectRequests)
  await page.waitForTimeout(2500)
  assert.equal(await page.evaluate(()=>window.__CC2_PREVIEW.objectRequests),count,'idle does not poll for objects')
  assert.deepEqual(errors,[])
  console.log('PASS: physical map positions, polygons, live highlight without auto-selection, original IDs, confirmation/cancel, keyboard, themes, mobile, unknown positions, idle state, one-card Job page, camera button and no shift between tabs at 1440 and 390 px')
} finally {await browser.close();await demo.cleanup()}
