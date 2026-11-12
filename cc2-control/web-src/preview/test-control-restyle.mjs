import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { mkdir, copyFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fixture, launchOptions } from './fixture.mjs'

const output = process.env.CC2_RESTYLE_OUTPUT
const setup = [
 "window.__CC2_PREVIEW.preferences.language='en';window.__CC2_PREVIEW.preferences.theme='obsidian';",
 "window.__TOOLS={busy:false,generation:0,error:'none',mode:0,active:null,original:null,configured:28.8,saved:29.088,editable:true,pending_restart:false,pending_conflict:false,reports:[],cell:{stage:0,owned:false,max_grams:1000,counts_per_gram:26.38,tare_counts:75939,grams:100}};",
 "window.__RESTYLE={printing:false,nozzle:24.6,bed:23.1,chamber:22,expansion:true,homed:''};window.__ACTIONS=[];window.__CONSOLE=[];window.__SHAPER={busy:false,command:'',output:'',completed:false,success:false};",
 "const original=window.fetch;const reply=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'Content-Type':'application/json'}});",
 "window.fetch=async(input,options={})=>{const url=String(input),s=window.__RESTYLE,t=window.__TOOLS;",
 "if(url.endsWith('/api/console'))return reply(window.__SHAPER);",
 "if(url.endsWith('/api/console/command')){window.__CONSOLE.push(String(options.body));Object.assign(window.__SHAPER,{busy:true,command:String(options.body),output:'Measuring...'});return reply({accepted:true},202);}",
 "if(url.endsWith('/api/health'))return reply({service:'cc2-control',version:'1.2.25',uptime_seconds:3600,loadavg:'0.25 0.30 0.28',mem_available_kb:52000,mem_total_kb:111168});",
 "if(url.endsWith('/api/calibration-tools/status'))return reply(t);",
 "if(url.endsWith('/api/calibration-tools/action')){const action=String(options.body);window.__ACTIONS.push(action);t.generation++;",
 "if(action==='rotation:read'){t.active=29.088;t.mode=1}else if(action==='cell:read'){t.mode=4;t.reports=['// 0.2g (0.91%)']}else if(action==='cell:diagnostic'){t.mode=5;t.reports=['// Sensor reported no errors']}else throw new Error('Unexpected simulated calibration '+action);return reply({accepted:true},202);}",
 "if(url.endsWith('/api/cavity-fan-floor'))return reply({installed:true,ready:true,printing:s.printing,minimum:null});",
 "if(url.endsWith('/api/nozzle-expansion'))return reply({enabled:s.expansion,available:true,editable:true,state:'idle',table:null,pending_restart:false});",
 "const response=await original(input,options);if(url.endsWith('/api/printer')){const d=await response.json();d.machine.status=s.printing?2:1;d.machine.status_name=s.printing?'Printing':'Idle';d.machine.progress=s.printing?38:0;d.print={...d.print,state:s.printing?'printing':'',filename:s.printing?'CC2_Community_Print_Example.gcode':'',enabled:s.printing,current_layer:s.printing?76:0,total_layers:s.printing?200:0,duration:s.printing?2100:0,remaining:s.printing?1200:0,total_duration:s.printing?3300:0,native:{state:s.printing?'printing':'standby'}};d.extruder={temperature:s.nozzle,target:0};d.heater_bed={temperature:s.bed,target:0};d.chamber={temperature:s.chamber};d.motion.homed_axes=s.homed;d.tuning={speed_percent:100,flow_percent:100,pressure_advance:0.035,live_velocity:0};return reply(d)}return response;};",
 "document.addEventListener('DOMContentLoaded',()=>{const b=[...document.querySelectorAll('body>div')].find(e=>e.textContent.startsWith('ANTEPRIMA'));if(b){b.textContent='PREVIEW · simulated data · no printer connected';b.style.bottom='4px';b.style.left='auto';b.style.right='12px';b.style.transform='none';b.style.padding='3px 8px';b.style.fontSize='10px';}});",
].join('\n')
const demo = await fixture(setup)
const browser = await chromium.launch(launchOptions)
try {
 const page = await browser.newPage({viewport:{width:1440,height:1100}})
 const errors=[];page.on('pageerror',error=>errors.push(error.message))
 await page.goto(demo.url+'#control')
 await page.locator('.cc2-rotation-tools').waitFor()
 assert.equal(await page.locator('.cc2-movement .cc2-shaper').count(),1)
 assert.equal(await page.locator('.cc2-tuning .cc2-rotation-tools').count(),1)
 assert.equal(await page.getByRole('button',{name:'Apply temporarily',exact:true}).isVisible(),true,'the complete rotation calculator stays visible')
 const aligned=await page.evaluate(()=>{const b=s=>document.querySelector(s).getBoundingClientRect();return {movement:b('.cc2-movement').bottom,fans:b('.cc2-fans').bottom,tuning:b('.cc2-tuning').bottom,temperature:b('.cc2-temperatures').bottom,offset:b('.cc2-live-offset').bottom,shaper:b('.cc2-shaper').height}})
 assert.ok(Math.max(aligned.movement,aligned.fans,aligned.tuning)-Math.min(aligned.movement,aligned.fans,aligned.tuning)<2,'main control cards share their lower edge')
 assert.ok(Math.abs(aligned.temperature-aligned.offset)<2,'temperature and offset rows align')
 assert.ok(aligned.shaper>=190&&aligned.shaper<300,'Input Shaper has a moderate height without a large empty area')
 assert.ok(await page.locator('.cc2-movement').getByText('Motion requires homing. Commands are disabled until the printer is ready.',{exact:true}).count()===1,'homing guidance is visible while axes are not ready')
 assert.equal(await page.locator('.cc2-motion-pad button:enabled').count(),0,'motion buttons remain disabled before homing')
 assert.equal(await page.getByRole('button',{name:'Read load cell',exact:true}).count(),0)
 const row=await Promise.all(['.cc2-extruder','.cc2-pid','.cc2-machine'].map(s=>page.locator(s).boundingBox()))
 assert.ok(row[0].x<row[1].x&&row[1].x<row[2].x,'extruder replaces the old Shaper card')
 const profiles=await page.locator('.cc2-material-presets').boundingBox()
 const columns=await page.locator('.cc2-control-columns').boundingBox()
 assert.ok(Math.abs(profiles.width-columns.width)<2,'profiles use the full page width')
 await page.getByRole('button',{name:'Read active distance',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('.cc2-rotation-tools').textContent.includes('29.088 mm'))
 assert.deepEqual(await page.evaluate(()=>window.__ACTIONS),['rotation:read'])
 if(output){
  await mkdir(output,{recursive:true})
  await page.evaluate(()=>window.scrollTo(0,0));await page.waitForTimeout(100)
  await page.screenshot({path:path.join(output,'control-desktop.png'),fullPage:true})
  await copyFile(new URL(demo.url),path.join(output,'Preview.html'))
 }
 await page.evaluate(()=>window.__RESTYLE.homed='xyz')
 await page.waitForFunction(()=>!document.querySelector('.cc2-shaper button').disabled)
 assert.equal(await page.locator('.cc2-movement').getByText('Motion requires homing. Commands are disabled until the printer is ready.',{exact:true}).count(),0,'homing guidance clears only when the axes are ready')
 const shaper=page.locator('.cc2-movement .cc2-shaper').getByRole('button')
 await shaper.click();await page.getByRole('button',{name:'Cancel',exact:true}).click()
 assert.deepEqual(await page.evaluate(()=>window.__CONSOLE),[])
 await shaper.click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('.cc2-shaper').textContent.includes('Measuring...'))
 assert.deepEqual(await page.evaluate(()=>window.__CONSOLE),['SHAPER_CALIBRATE'])
 assert.equal(await shaper.isDisabled(),true)
 await page.evaluate(()=>Object.assign(window.__SHAPER,{busy:false,completed:true,success:true,output:'Suggested shaper X'}))
 await page.waitForFunction(()=>document.querySelector('.cc2-shaper').textContent.includes('Suggested shaper X'))
 await page.evaluate(()=>window.__RESTYLE.homed='')
 await page.waitForFunction(()=>document.querySelector('.cc2-shaper button').disabled)
 await page.evaluate(()=>window.__RESTYLE.homed='xyz')
 await page.waitForFunction(()=>!document.querySelector('.cc2-shaper button').disabled)
 await page.goto(demo.url+'#bed/cell')
 await page.getByRole('tab',{name:'Load cell',exact:true}).waitFor()
 assert.equal(await page.getByRole('tab',{name:'Load cell',exact:true}).getAttribute('aria-selected'),'true')
 assert.equal(await page.getByRole('button',{name:'Read active distance',exact:true}).count(),0)
 await page.getByRole('button',{name:'Read load cell',exact:true}).click()
 await page.getByText('// 0.2g (0.91%)',{exact:true}).waitFor()
 await page.getByRole('button',{name:'Run diagnosis',exact:true}).click()
 await page.getByText('// Sensor reported no errors',{exact:true}).waitFor()
 await page.getByRole('button',{name:'Guided calibration',exact:true}).click()
 await page.getByRole('button',{name:'Cancel',exact:true}).click()
 assert.deepEqual(await page.evaluate(()=>window.__ACTIONS),['rotation:read','cell:read','cell:diagnostic'])
 if(output)await page.screenshot({path:path.join(output,'load-cell-desktop.png'),fullPage:true})
 await page.getByRole('tab',{name:'Nozzle expansion',exact:true}).click()
 await page.locator('[data-nozzle-expansion]').waitFor()
 await page.goto(demo.url+'#job')
 const temps=page.locator('.cc2-job-temperatures')
 await temps.waitFor();assert.match(await temps.textContent(),/24.6/);assert.match(await temps.textContent(),/23.1/)
 await page.evaluate(()=>Object.assign(window.__RESTYLE,{nozzle:210.5,bed:60.2,chamber:31.4}))
 await page.waitForFunction(()=>document.querySelector('.cc2-job-temperatures').textContent.includes('210.5'))
 assert.match(await temps.textContent(),/60.2/);assert.match(await temps.textContent(),/31.4/)
 if(output)await page.screenshot({path:path.join(output,'job-desktop.png'),fullPage:true})
 await page.evaluate(()=>window.__RESTYLE.printing=true)
 await page.goto(demo.url+'#dashboard');await page.locator('.cc2-dashboard-primary').waitFor()
 assert.equal(await page.locator('.cc2-dashboard-job').count(),0,'original Dashboard structure restored')
 assert.equal(await page.locator('.cc2-dashboard .cc2-tuning input[type=range]').count(),2)
 if(output)await page.screenshot({path:path.join(output,'dashboard-desktop.png'),fullPage:true})
 for(const width of [1920,1280,1024,768,390]){
  await page.setViewportSize({width,height:1100})
  for(const route of ['control','bed/cell','bed/nozzle','job','dashboard']){
   await page.goto(demo.url+'#'+route);await page.waitForTimeout(180)
   assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),route+' overflows at '+width)
   if(width===390&&output&&route==='control')await page.screenshot({path:path.join(output,'control-mobile.png'),fullPage:true})
  }
 }
 const italian=await fixture(setup.replace("preferences.language='en'","preferences.language='it'"))
 try {
  for(const width of [1440,390]){
   await page.setViewportSize({width,height:1100})
   for(const route of ['control','bed/cell','bed/nozzle','job','dashboard']){
    await page.goto(italian.url+'#'+route);await page.waitForTimeout(250)
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Italian '+route+' overflows at '+width)
   }
  }
 } finally {await italian.cleanup()}
 assert.deepEqual(errors,[])
 if(output)await writeFile(path.join(output,'layout-check.json'),JSON.stringify({passed:true,widths:[1920,1440,1280,1024,768,390],dashboard:'original',control_alignment:aligned,languages:['en','it'],printer_accessed:false},null,2)+'\n')
 console.log('PASS: relocated calibration controls, cell-tab navigation and real buttons, nozzle expansion, live job temperatures, restored original Dashboard and responsive layout. No printer accessed.')
} finally {await browser.close();await demo.cleanup()}
