import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { chromium } from 'playwright'
import { fixture, launchOptions, root } from './fixture.mjs'

const setup = `{
 const s=window.__CC2_PREVIEW;
 const tracking=localStorage.getItem('canvas-layout-tracking')==='on';
 s.trays.forEach((t,i)=>Object.assign(t,{status:1,filament_name:'PLA',filament_type:'PLA',filament_color:['#000000','#FF8A3D','#48A7FA','#F5F5F5'][i]}));
 s.spoolLibrary={available:true,enabled:tracking,revision:1,canvas:true,active_tray:-1,job:{active:false,slots:[]},log:[],
 slots:[0,1,2,3,4].map(i=>({slot:i,spool:i<4?'spool-'+i:'',question:'',last:'',printer:null})),
 spools:[0,1,2,3].map(i=>({id:'spool-'+i,name:['ELEGOO PLA nero','PLA Arancio','ELEGOO PLA blu','PLA bianco'][i],brand:'ELEGOO',material:'PLA',color:s.trays[i].filament_color,net:1000,remaining:[986,953,990,1014][i],archived:false}))};
}`
const demo = await fixture(setup)
const browser = await chromium.launch(launchOptions)
const output = process.env.CC2_CANVAS_SCREENSHOTS || path.join(root, '../build/canvas-layout')
await mkdir(output, { recursive: true })
const results = []
try {
  for (const width of [1600, 390]) for (const tracking of [true, false]) for (const theme of ['dark', 'light']) {
    if(process.env.CC2_CANVAS_ONLY && process.env.CC2_CANVAS_ONLY!==`${width}-${tracking?'tracking':'no-tracking'}-${theme}`) continue
    const page = await browser.newPage({ viewport: { width, height: width === 390 ? 844 : 1100 } })
    const errors = []
    page.on('pageerror', e => errors.push(e.message))
    await page.addInitScript(({ tracking, theme }) => {
      localStorage.setItem('canvas-layout-tracking', tracking ? 'on' : 'off')
      localStorage.setItem('cc2-fixture-preferences', JSON.stringify({ language: 'it', theme }))
    }, { tracking, theme })
    await page.goto(demo.url + '#canvas')
    const card = n => page.locator(`[data-canvas-slot="${n}"]`)
    const pick = n => card(n).getByRole('button', { name: 'Seleziona slot', exact: true })
    const load = page.locator('[data-canvas-action="canvas.load"]')
    const unload = page.locator('[data-canvas-action="canvas.unload"]')
    await card(1).getByText('Presente · scaricato dalla testina', { exact: true }).waitFor()
    if (tracking) await card(0).getByText('986 g', { exact: true }).waitFor()
    assert.equal(await page.locator('.cc2-canvas-choose').count(), tracking ? 4 : 0)
    assert.equal(await page.locator('[data-canvas-spool-name]').count(), tracking ? 4 : 0)
    if (!tracking) assert(!/\d+ g/.test(await page.locator('.cc2-canvas-slots').textContent()), 'no invented weights with tracking off')
    await pick(2).focus()
    await page.keyboard.press('Enter')
    await page.waitForFunction(() => document.querySelector('[data-canvas-slot="2"] button').getAttribute('aria-pressed') === 'true')
    assert.match(await page.locator('[data-canvas-actions]').textContent(), /Slot 3/)
    assert.equal(await load.isDisabled(), false)
    assert.equal(await unload.isDisabled(), true)
    await pick(0).click()
    const geometry = await page.evaluate(() => {
      const a=document.querySelector('[data-canvas-settings]').getBoundingClientRect()
      const b=document.querySelector('[data-canvas-timeline]').getBoundingClientRect()
      return { page:document.documentElement.scrollHeight, width:innerWidth, scroll:document.documentElement.scrollWidth, settings:a.height, history:b.height, sideBySide:Math.abs(a.top-b.top)<1 }
    })
    const contrast = await page.locator('.cc2-canvas-connection').evaluate(e=>{
      const style=getComputedStyle(e)
      const lum=colour=>colour.match(/[\d.]+/g).slice(0,3).map(Number).map(v=>v/255).map(v=>v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4).reduce((a,v,i)=>a+v*[0.2126,0.7152,0.0722][i],0)
      const a=lum(style.color),b=lum(style.backgroundColor)
      return (Math.max(a,b)+0.05)/(Math.min(a,b)+0.05)
    })
    assert(contrast>=4.5, `Connection badge contrast ${contrast}: ${theme}`)
    assert(geometry.scroll <= width, JSON.stringify(geometry))
    if (geometry.sideBySide) assert(Math.abs(geometry.settings-geometry.history)<1, JSON.stringify(geometry))
    await page.evaluate(()=>window.scrollTo(0,0)); await page.screenshot({ path:path.join(output, `${width}-${tracking?'tracking':'no-tracking'}-${theme}.png`), fullPage:true })
    if (tracking && theme === 'dark') {
      // Actual polling/readback path, not an injected UI store: 28 changes fill
      // the existing 24-entry history without increasing the page/card height.
      for(let i=0;i<7;i++) {
        const state=i%2===0?'empty':'present'
        await page.evaluate(i => window.__CC2_PREVIEW.trays.forEach(t=>{t.status=i%2===0?0:1}),i)
        await page.waitForFunction(state => [...document.querySelectorAll('[data-canvas-slot-state]')].every(e=>e.textContent.trim()===(state==='empty'?'Vuoto':'Presente · scaricato dalla testina')),state)
      }
      assert.equal(await page.locator('[data-canvas-event]').count(),24)
      const after=await page.evaluate(()=>({page:document.documentElement.scrollHeight,height:document.querySelector('[data-canvas-timeline]').getBoundingClientRect().height,scrolls:document.querySelector('.cc2-canvas-history-scroll').scrollHeight>document.querySelector('.cc2-canvas-history-scroll').clientHeight}))
      assert.equal(after.page,geometry.page,JSON.stringify({geometry,after,blocks:await page.locator('.cc2-canvas-slots,[data-canvas-actions],[data-canvas-settings],[data-canvas-timeline]').evaluateAll(es=>es.map(e=>({class:e.className,height:e.getBoundingClientRect().height})))}))
      assert.equal(after.height,geometry.history)
      assert(after.scrolls)
      await page.locator('.cc2-canvas-history-scroll').focus()
      await page.keyboard.press('End')
      await page.waitForFunction(()=>document.querySelector('.cc2-canvas-history-scroll').scrollTop>0)
      await page.locator('.cc2-canvas-history-scroll').evaluate(e=>{e.scrollTop=0;e.blur()})
      await page.evaluate(()=>window.scrollTo(0,0)); await page.screenshot({path:path.join(output, `${width}-history.png`),fullPage:true})
      await page.getByRole('button',{name:'Cancella cronologia',exact:true}).click()
      assert.equal(await page.locator('[data-canvas-event]').count(),0)
      await page.evaluate(()=>window.__CC2_PREVIEW.trays.forEach(t=>{t.status=1}))
      await card(0).getByText('Presente · scaricato dalla testina',{exact:true}).waitFor()
    }
    // Loaded/other slot, printing, recovery, stale and disconnected states.
    await page.evaluate(()=>{const s=window.__CC2_PREVIEW;s.trays[1].status=2;s.activeTray=1;s.headLoaded=true})
    await card(1).getByText('Caricato nella testina',{exact:true}).waitFor()
    await pick(1).click()
    assert.equal(await load.isDisabled(),true)
    assert.equal(await unload.isDisabled(),false)
    await pick(2).click()
    assert.equal(await load.isDisabled(),true)
    assert.equal(await unload.isDisabled(),true)
    await page.evaluate(()=>window.__CC2_PREVIEW.status=2)
    await page.waitForFunction(()=>document.querySelector('[data-canvas-action="canvas.select_material"]').disabled)
    await page.evaluate(()=>{const s=window.__CC2_PREVIEW;s.status=1;s.activeTray=-1;s.trays.forEach(t=>{t.status=1})})
    await page.locator('[data-canvas-recovery]').waitFor()
    assert.equal(await load.isDisabled(),true)
    assert.equal(await unload.isDisabled(),true)
    assert.equal(await page.locator('[data-canvas-recovery-load]').isDisabled(),false)
    await page.evaluate(()=>window.scrollTo(0,0)); await page.screenshot({path:path.join(output, `${width}-${tracking?'tracking':'no-tracking'}-${theme}-recovery.png`),fullPage:true})
    await page.evaluate(()=>{window.__CC2_PREVIEW.canvasFresh=false})
    await card(1).getByText('Stato non disponibile',{exact:true}).waitFor()
    assert.equal(await page.locator('[data-canvas-recovery]').count(),0)
    assert.equal(await load.isDisabled(),true)
    assert.equal(await unload.isDisabled(),true)
    await page.evaluate(()=>{window.__CC2_PREVIEW.canvasConnected=false})
    await page.getByText('Il firmware attuale della stampante non espone il controllo Canvas.',{exact:true}).waitFor()
    assert.equal(await pick(1).isDisabled(),true)
    assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),[],'layout, selection, history and readback send no motor commands')
    const finalWidth=await page.evaluate(()=>document.documentElement.scrollWidth)
    assert(finalWidth<=width)
    assert.deepEqual(errors,[])
    results.push({width,tracking,theme,geometry,states:'present/empty/loaded/other-slot/printing/recovery/stale/offline',passed:true})
    await page.close()
  }
  await writeFile(path.join(output,'validation.json'),JSON.stringify(results,null,2))
  console.log('PASS: Canvas layout desktop/mobile × tracking on/off × dark/light; native state guards, bounded scrolling, keyboard selection and no automatic commands')
}finally{await browser.close();await demo.cleanup()}
