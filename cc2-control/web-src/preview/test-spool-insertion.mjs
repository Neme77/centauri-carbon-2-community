import assert from 'node:assert/strict'
import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

// Exercise the actual global insertion dialog and its HTTP payload, not internal UI helpers.
const setup = `{
 const s=window.__CC2_PREVIEW;
 s.posts=[];
 const spool={id:'source',name:'ELEGOO PLA Black',brand:'ELEGOO',material:'PLA',color:'#000000',net:1000,remaining:280,tare:220,low:100,diameter:1.75,density:1.24,price:20,note:'Product details',spoolman_id:'',created:1,used:720,archived:false};
 s.spoolLibrary={available:true,error:'',enabled:true,revision:1,canvas:true,active_tray:-1,job:{active:false,file:'',started:0,slots:[]},log:[],spools:[{...spool,spoolman_id:'42'},{...spool,id:'other',name:'Other spool',net:750,remaining:150,tare:200},{...spool,id:'archived',name:'Archived',archived:true}],slots:[0,1,2,3,4].map(slot=>({slot,spool:slot===0?'source':'',last:'',question:slot===1?'inserted':'',question_since:1,question_mm:0,runout:false,printer:{status:1,type:'PLA',name:'PLA',brand:'ELEGOO',color:'#000000',code:''}}))};
 const original=window.fetch;
 window.fetch=async (input,options={})=>{
  const path=new URL(typeof input==='string'?input:input.url,location.href).pathname;
  const reply=(value,status=200)=>new Response(JSON.stringify(value),{status,headers:{'Content-Type':'application/json'}});
  if(path.endsWith('/api/spools')) return reply(s.spoolLibrary);
  if(path.includes('/api/spools/')&&options.method==='POST'){
   s.posts.push({path,body:String(options.body),marker:new Headers(options.headers).get('X-CC2-Request')});
   if(s.reject) return reply({error:'Fixture refusal'},409);
   if(path.endsWith('/assign')) {
    const fields=new URLSearchParams(String(options.body).trim().split('\\n').join('&'));
    Object.assign(s.spoolLibrary.slots[Number(fields.get('slot'))],{spool:fields.get('spool'),question:''});
    s.spoolLibrary.revision++;
    return reply({assigned:true});
   }
   if(path.endsWith('/save')) {
    const fields=Object.fromEntries(String(options.body).split('\\n').map(line=>{const n=line.indexOf('=');return [line.slice(0,n),line.slice(n+1)]}));
    const id='created-'+s.spoolLibrary.spools.length;
    s.spoolLibrary.spools.push({...fields,id,net:Number(fields.net),remaining:Number(fields.remaining),tare:Number(fields.tare),archived:false,used:0,created:2});
    Object.assign(s.spoolLibrary.slots[Number(fields.slot)],{spool:id,question:''});
    s.spoolLibrary.revision++;
    return reply({saved:true,id});
   }
  }
  if(path.endsWith('/api/control')&&options.method==='POST'&&s.status===2){
   const action=String(options.body);
   s.commands.push(action);
   const [,slot,type,hex,name,brand]=action.match(/^canvas:material:([0-3]):([^:]+):([0-9a-f]{6}):\\d+:\\d+:([^:]+):([^:]+)$/i)||[];
   if(!slot||Number(slot)===s.activeTray||s.trays[Number(slot)].status!==1) return reply({error:'Fixture refusal'},409);
   const color='#'+hex.toUpperCase();
   Object.assign(s.trays[Number(slot)],{filament_type:type,filament_name:name,filament_color:color});
   Object.assign(s.spoolLibrary.slots[Number(slot)].printer,{type,name,brand,color});
   return reply({accepted:true});
  }
  const response=await original(input,options);
  if(path.endsWith('/api/printer')){
   const data=await response.json();data.spools={enabled:true,revision:s.spoolLibrary.revision,questions:s.spoolLibrary.slots.filter(x=>x.question).map(x=>x.slot)};return reply(data);
  }
  return response;
 };
}`
const demo=await fixture()
const fixtureFile=new URL(demo.url)
await writeFile(fixtureFile,(await readFile(fixtureFile,'utf8')).replace('</script>',';'+setup+'</script>'))
const browser=await chromium.launch(launchOptions)
const output=process.env.CC2_SPOOL_SCREENSHOTS
if(output) await mkdir(output,{recursive:true})
try {
 for(const width of [1440,390]) for(const lang of ['en','it']) for(const theme of ['dark','light']) {
  const dict=JSON.parse(await readFile(new URL(`../public/locales/${lang}.json`,import.meta.url),'utf8'))
  const page=await browser.newPage({viewport:{width,height:1000}})
  const errors=[];page.on('pageerror',e=>errors.push(e.message))
  await page.addInitScript(({lang,theme})=>localStorage.setItem('cc2-fixture-preferences',JSON.stringify({language:lang,theme})),{lang,theme})
  await page.goto(demo.url+'#dashboard')
  const dialog=page.getByRole('dialog')
  await dialog.getByText(dict['spools.which_spool'],{exact:true}).waitFor()
  const before=await page.evaluate(()=>JSON.stringify(window.__CC2_PREVIEW.spoolLibrary.spools[0]))
  const confirm=dialog.getByRole('button',{name:dict['common.confirm'],exact:true})
  await dialog.getByText(dict['spools.add_same'],{exact:true}).click()
  const source=dialog.getByLabel(dict['spools.insert_copy_source'],{exact:true})
  await source.selectOption('source')
  assert.equal(await source.locator('option[value="archived"]').count(),0)
  const net=dialog.getByLabel(dict['spools.net'],{exact:true})
  const remaining=dialog.getByLabel(dict['spools.remaining_g'],{exact:true})
  const tare=dialog.getByLabel(dict['spools.tare'],{exact:true})
  const total=dialog.getByLabel(dict['spools.weigh_gross'],{exact:true})
  assert.equal(await remaining.inputValue(),'1000','copy starts full, not with source remaining/used')
  await total.fill('920.5')
  assert.equal(await remaining.inputValue(),'700.5')
  assert.equal(await remaining.getAttribute('readonly'),'')
  assert.equal(await net.inputValue(),'1000','scale reading must not rewrite nominal capacity')
  await tare.fill('200.5');assert.equal(await remaining.inputValue(),'720')
  await total.fill('100');assert.equal(await confirm.isDisabled(),true)
  await total.fill('920.5');await tare.fill('');assert.equal(await confirm.isDisabled(),true)
  await tare.fill('5001');assert.equal(await confirm.isDisabled(),true)
  await tare.fill('220');await total.fill('25001');assert.equal(await confirm.isDisabled(),true)
  await total.fill('220');assert.equal(await remaining.inputValue(),'0');assert.equal(await confirm.isDisabled(),false)
  await total.fill('');assert.equal(await remaining.getAttribute('readonly'),null)
  await remaining.fill('650');assert.equal(await confirm.isDisabled(),false)
  await total.fill('920.5')
  assert.deepEqual(await page.evaluate(()=>window.__CC2_PREVIEW.posts),[],'selection and weighing send nothing')
  if(output) await dialog.screenshot({path:`${output}/${width}-${lang}-${theme}.png`})
  // A rejected save leaves the dialog open; retry is explicit and does not move the original spool.
  await page.evaluate(()=>{window.__CC2_PREVIEW.reject=true})
  await confirm.click();await page.getByText(/Fixture refusal/).waitFor()
  assert.equal(await dialog.count(),1)
  await page.evaluate(()=>{window.__CC2_PREVIEW.reject=false})
  await confirm.click();await dialog.waitFor({state:'hidden'})
  const state=await page.evaluate(()=>({source:JSON.stringify(window.__CC2_PREVIEW.spoolLibrary.spools[0]),slots:window.__CC2_PREVIEW.spoolLibrary.slots,posts:window.__CC2_PREVIEW.posts,commands:window.__CC2_PREVIEW.commands}))
  assert.equal(state.source,before)
  assert.equal(state.slots[0].spool,'source')
  assert.match(state.slots[1].spool,/^created-/)
  assert.equal(state.posts.length,2)
  for(const post of state.posts){
   assert(post.path.endsWith('/api/spools/save'));assert.equal(post.marker,'1')
   const fields=Object.fromEntries(post.body.split('\n').map(s=>{const i=s.indexOf('=');return[s.slice(0,i),s.slice(i+1)]}))
   assert.equal(fields.id,undefined);assert.equal(fields.spoolman_id,'','a copy of a linked spool is a new spool: no inherited Spoolman link');assert.equal(fields.slot,'1');assert.equal(fields.net,'1000');assert.equal(fields.remaining,'700.5');assert.equal(fields.tare,'220');assert.equal(fields.used,undefined)
  }
  assert.deepEqual(state.commands,[])
  // A second insertion still supports creating from scratch with manual remaining weight.
  await page.evaluate(()=>{const s=window.__CC2_PREVIEW;Object.assign(s.spoolLibrary.slots[2],{question:'inserted',question_since:2});s.spoolLibrary.revision++})
  await dialog.getByText(dict['spools.which_spool'],{exact:true}).waitFor()
  await dialog.getByText(dict['spools.new_spool'],{exact:true}).click()
  const manual=dialog.getByLabel(dict['spools.remaining_g'],{exact:true})
  assert.equal(await manual.getAttribute('readonly'),null)
  await manual.fill('450')
  await dialog.getByLabel(dict['spools.tare'],{exact:true}).fill('200')
  await confirm.click();await dialog.waitFor({state:'hidden'})
  const manualPost=await page.evaluate(()=>window.__CC2_PREVIEW.posts.at(-1))
  assert(manualPost.body.includes('remaining=450\n'));assert(manualPost.body.endsWith('slot=2'))
  // Existing inventory form retains its original manual controls, without the insertion-only field.
  await page.evaluate(()=>{location.hash='#spools'})
  await page.getByRole('button',{name:dict['spools.add'],exact:true}).click()
  await dialog.getByLabel(dict['spools.remaining_g'],{exact:true}).waitFor()
  assert.equal(await dialog.getByLabel(dict['spools.weigh_gross'],{exact:true}).count(),0)
  assert.equal(await dialog.getByLabel(dict['spools.remaining_g'],{exact:true}).getAttribute('readonly'),null)
  await dialog.getByRole('button',{name:dict['common.cancel'],exact:true}).click()
  assert.equal(await page.evaluate(()=>window.__CC2_PREVIEW.posts.length),3,'cancelled inventory form sends nothing')
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true)
  assert.deepEqual(errors,[])
  await page.close()
 }
 const live=await fixture(setup+`{
  const s=window.__CC2_PREVIEW;
  Object.assign(s,{status:2,activeTray:0,headLoaded:true});
  s.trays[0].status=2;
  s.trays[1].status=0;
  s.spoolLibrary.slots[0].printer.status=2;
  Object.assign(s.spoolLibrary.slots[1],{question:'',last:'spare'});
  s.spoolLibrary.slots[1].printer.status=0;
  s.spoolLibrary.spools.push({...s.spoolLibrary.spools[0],id:'spare',spoolman_id:'',name:'Green PLA',color:'#388052'});
 }`)
 try {
  const page=await browser.newPage()
  await page.addInitScript(()=>localStorage.setItem('cc2-fixture-preferences','{"language":"en"}'))
  await page.goto(live.url+'#spools')
  const standby=page.locator('[data-slot="1"]')
  await standby.getByText('Empty', {exact:true}).waitFor()
  await page.evaluate(()=>{
   const s=window.__CC2_PREVIEW
   s.trays[1].status=1
   Object.assign(s.spoolLibrary.slots[1],{question:'inserted',question_since:2})
   s.spoolLibrary.slots[1].printer.status=1
   s.spoolLibrary.revision++
  })
  const dialog=page.getByRole('dialog')
  await dialog.getByText('Green PLA',{exact:true}).click()
  await dialog.getByRole('checkbox').waitFor()
  assert.equal(await dialog.getByRole('checkbox').isChecked(),true)
  await dialog.getByRole('button',{name:'Confirm',exact:true}).click()
  await dialog.waitFor({state:'hidden'})
  await standby.getByText('Green PLA',{exact:true}).waitFor()
  const result=await page.evaluate(()=>{
   const s=window.__CC2_PREVIEW
   return {active:s.spoolLibrary.slots[0].spool,standby:s.spoolLibrary.slots[1].spool,commands:s.commands}
  })
  assert.equal(result.active,'source')
  assert.equal(result.standby,'spare')
  assert.equal(result.commands.length,1)
  assert.match(result.commands[0],/^canvas:material:1:PLA:388052:/)
  await page.close()
 } finally {
  await live.cleanup()
 }
 console.log('PASS: insertion copy and weighing; choosing a standby spool during a print updates its assignment and material')
}finally{await browser.close();await demo.cleanup()}
