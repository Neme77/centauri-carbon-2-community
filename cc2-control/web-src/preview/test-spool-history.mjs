import assert from 'node:assert/strict'
import { readFile, mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'
const setup = `{
 const s=window.__CC2_PREVIEW;
 s.spoolPosts=[];
 const spool={id:'source',name:'PLA with a long descriptive product name',brand:'ELEGOO',material:'PLA',color:'#48A7FA',net:1000,remaining:280,tare:220,low:100,diameter:1.75,density:1.24,price:20,note:'',created:1,used:720,archived:false};
 s.spoolLibrary={available:true,error:'',enabled:false,revision:1,canvas:true,active_tray:-1,job:{active:false,file:'',started:0,slots:[]},spools:[spool],slots:[],log:Array.from({length:150},(_,i)=>({time:1792860000-i*60,spool:i===1?'deleted':i===2?'':'source',slot:i%5,kind:i<40?['new','correction','runout'][i%3]:'print',grams:i<40?10:-12,mm:120,job:'history-'+i+'-very-long-filename-'+ 'x'.repeat(90)+'.gcode',result:i%2?'complete':'cancelled'}))};
 const original=window.fetch;
 window.fetch=async(input,options={})=>{
  const path=new URL(typeof input==='string'?input:input.url,location.href).pathname;
  if(path.startsWith('/api/spools')&&options.method==='POST') s.spoolPosts.push({path,body:options.body});
  return original(input,options);
 };
}`
const demo=await fixture(setup)
const browser=await chromium.launch(launchOptions)
const output=process.env.CC2_SPOOL_HISTORY_SCREENSHOTS
if(output) await mkdir(output,{recursive:true})
try {
 for(const width of [1440,390]) for(const language of ['en','it']) for(const theme of ['dark','light']) {
  const dict=JSON.parse(await readFile(new URL(`../public/locales/${language}.json`,import.meta.url),'utf8'))
  const page=await browser.newPage({viewport:{width,height:1000},hasTouch:width===390})
  const errors=[];page.on('pageerror',e=>errors.push(e.message))
  await page.addInitScript(({language,theme})=>localStorage.setItem('cc2-fixture-preferences',JSON.stringify({language,theme})),{language,theme})
  await page.goto(demo.url+'#spools')
  const card=page.locator('[data-recent-history]')
  const filter=card.getByLabel(dict['spools.history_filter'],{exact:true})
  const region=()=>card.getByRole('region',{name:dict['spools.recent'],exact:true})
  const rows=()=>region().locator('li[data-event]')
  await filter.waitFor({timeout:10000}).catch(async e=>{console.log('BROWSER', errors, await page.locator('body').innerText());throw e})
  const before=await page.evaluate(()=>JSON.stringify(window.__CC2_PREVIEW.spoolLibrary))
  assert.equal(await filter.inputValue(),'all')
  assert.equal(await rows().count(),30)
  assert.match(await rows().first().innerText(),/history-0-/)
  await card.getByText(dict['spools.history_count'].replace('{shown}','30').replace('{total}','150'),{exact:true}).waitFor()
  await card.getByText(dict['spools.deleted_spool'],{exact:true}).waitFor()
  await card.getByText(dict['spools.untracked'],{exact:true}).waitFor()
  await card.scrollIntoViewIfNeeded()
  const size=await region().evaluate(e=>({height:e.clientHeight,total:e.scrollHeight,overflow:getComputedStyle(e).overflowY}))
  assert(size.height<=321&&size.total>size.height,'long history stays inside a bounded panel')
  assert.equal(size.overflow,'auto')
  await region().focus();await page.keyboard.press('End')
  await page.waitForFunction(()=>document.querySelector('[data-recent-history] section').scrollTop>0)
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true)
  await filter.selectOption('consumption')
  assert.equal(await rows().count(),30)
  assert.deepEqual(await rows().evaluateAll(xs=>[...new Set(xs.map(x=>x.dataset.event))]),['print'])
  assert.match(await rows().first().innerText(),/history-40-/,'filter searches beyond the first 30 mixed events')
  assert.match(await rows().last().innerText(),/history-69-/)
  await card.getByText(dict['spools.history_count'].replace('{shown}','30').replace('{total}','110'),{exact:true}).waitFor()
  assert.equal(await region().evaluate(e=>e.scrollTop),0)
  if(output) await card.screenshot({path:`${output}/${width}-${language}-${theme}.png`})
  await filter.selectOption('all')
  assert.match(await rows().first().innerText(),/history-0-/)
  assert.equal(await page.evaluate(()=>JSON.stringify(window.__CC2_PREVIEW.spoolLibrary)),before,'view changes never alter accounting or stored history')
  assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.spoolPosts),[])
  assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.commands),[])
  assert.deepEqual(errors,[])
  await page.close()
 }
 for(const log of [[],[{time:1,spool:'',slot:-1,kind:'correction',grams:0,mm:0,job:'',result:''}],[{time:1,spool:'',slot:-1,kind:'print',grams:0,mm:0,job:'',result:''}]]) {
  const small=await fixture(setup+`;window.__CC2_PREVIEW.spoolLibrary.log=${JSON.stringify(log)};`)
  const page=await browser.newPage()
  await page.addInitScript(()=>localStorage.setItem('cc2-fixture-preferences',JSON.stringify({language:'en'})))
  await page.goto(small.url+'#spools')
  const dict=JSON.parse(await readFile(new URL('../public/locales/en.json',import.meta.url),'utf8'))
  const card=page.locator('[data-recent-history]')
  const filter=card.getByLabel(dict['spools.history_filter'])
  await filter.selectOption('consumption')
  if(log[0]?.kind==='print') assert.equal(await card.locator('li[data-event="print"]').count(),1)
  else await card.getByText(dict['spools.no_consumption'],{exact:true}).waitFor()
  await filter.selectOption('all')
  if(!log.length) await card.getByText(dict['spools.no_history'],{exact:true}).waitFor()
  else assert.equal(await card.locator('li[data-event]').count(),1)
  await page.close();await small.cleanup()
 }
 console.log('PASS: bounded history, keyboard scroll, filtering before limit, counts, empty/deleted/untracked entries, no mutations; desktop/mobile, EN/IT, dark/light')
}finally{await browser.close();await demo.cleanup()}
