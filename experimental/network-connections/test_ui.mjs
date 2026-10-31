import assert from 'node:assert/strict'
import {readFile,writeFile,mkdir} from 'node:fs/promises'
import {createRequire} from 'node:module'
import path from 'node:path'
const app=path.resolve(process.argv[2]),out=path.resolve(process.argv[3])
const require=createRequire(path.join(app,'web-src/package.json'))
const {chromium}=require('playwright')
const html=await readFile(path.join(app,'web/index.html'),'utf8')
const dict=async lang=>JSON.parse(await readFile(path.join(app,'web-src/public/locales',`${lang}.json`),'utf8'))
const en=await dict('en'),sent=[],errors=[]
let language='en',theme='obsidian',pending=null
let holdConfirm=false,releaseConfirm,failConfirm=false
const ip={enabled:true,method:'dhcp',ip:'',prefix:0,gateway:'',dns1:'',dns2:''}
const config={ethernet_support:true,selected:'wifi',wifi:{...ip},ethernet:{...ip}}
const actual={known:true,method:'dhcp',prefix:24,gateway:'192.0.2.1',dns1:'192.0.2.1',dns2:''}
const snapshot={schema:1,wifi:{present:true,enabled:true,carrier:true,address:'192.0.2.103'},ethernet:{present:true,enabled:true,carrier:true,address:'192.0.2.107',link_mbps:1000},wireless:{control_available:true,ssid:'Fixture home',security:'WPA2-PSK',signal_dbm:-47,frequency_mhz:5220},route:{selected:'wifi',source:'192.0.2.103',internet:'untested'},services:{camera:{compatibility_mapped:true},printer:{compatibility_mapped:true}},usb:{qualified_ue300:true,count:1}}
const browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH,args:['--no-sandbox','--disable-gpu']}: {})})
await mkdir(out,{recursive:true})
try {
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 page.on('pageerror',e=>errors.push(e.message))
 await page.route('**/*',async route=>{
  const r=route.request(),url=new URL(r.url())
  assert.equal(url.hostname,'192.0.2.107')
  if(url.pathname==='/')return route.fulfill({body:html,contentType:'text/html'})
  if(url.pathname.startsWith('/i18n/'))return route.fulfill({json:await dict(path.basename(url.pathname,'.json'))})
  if(url.pathname==='/api/preferences')return route.fulfill({json:{language,theme}})
  if(url.pathname==='/api/setup')return route.fulfill({json:{configured:true,mqtt_connected:true,snapshot_received:true}})
  if(url.pathname==='/api/network/manager')return route.fulfill({json:{installed:true,state_valid:true,ready:true,busy:Boolean(pending),last_exit:0,phase:pending?'pending':'idle',config,saved:config,actual:{wifi:actual,ethernet:actual},pending,networks:{uptime_ms:500,saved:[{ssid:'Saved fixture',network_id:2,current:false}],visible:[{ssid:'Visible fixture',security:'wpa2',signal:-50,compatible:true}]},diagnostic:'',last_error:''}})
  if(url.pathname==='/api/network')return route.fulfill({json:{available:true,fresh:true,age_ms:0,snapshot}})
  if(url.pathname==='/api/network/action' || url.pathname==='/api/network/scan'){
   assert.equal(r.headers()['x-cc2-lan'],undefined)
   assert.equal(r.headers()['x-cc2-request'],'1')
   const body=url.pathname.endsWith('/scan')?'scan':JSON.parse(r.postData())
   sent.push(body)
   if(body.op==='confirm' && failConfirm)return route.fulfill({status:500,json:{error:'fixture_confirmation_failed'}})
   if(body.op==='confirm' && holdConfirm)await new Promise(resolve=>{releaseConfirm=resolve})
   if(body.op==='confirm' || body.op==='cancel')pending=null
   return route.fulfill({status:202,json:{accepted:true,temporary:false}})
  }
  if(url.pathname==='/api/printer')return route.fulfill({json:{connected:true,machine:{status:1,status_name:'Idle'},print:{enabled:false},spools:{enabled:false,questions:[]},experiments:{},recovery:{available:false}}})
  return route.fulfill({json:{}})
 })
 const confirm=()=>page.getByRole('button',{name:en['common.confirm'],exact:true}).click()
 await page.goto('http://192.0.2.107/#settings/connection')
 assert.equal(await page.getByRole('button',{name:/^Prepare trial$/i,exact:true}).count(),0)
 assert(!(await page.locator('body').innerText()).includes('3 minutes'))
 await page.getByRole('button',{name:en['network.switch'],exact:true}).nth(1).click()
 await page.getByRole('button',{name:en['common.cancel'],exact:true}).click()
 assert.equal(sent.length,0)
 await page.getByRole('button',{name:en['network.switch'],exact:true}).nth(1).click();await confirm()
 await page.waitForFunction(()=>!document.querySelector('[role=dialog]'))
 assert.deepEqual(sent.at(-1),{op:'select',interface:'ethernet'})
 await page.getByRole('button',{name:en['network.configure_ip'],exact:true}).nth(1).click()
 const ipDialog=page.getByRole('dialog',{name:en['network.configure_ip'],exact:true})
 await ipDialog.waitFor()
 await ipDialog.getByRole('combobox').selectOption('static')
 for(const [key,value] of Object.entries({ip:'192.0.2.108',prefix:'24',gateway:'192.0.2.1',dns1:'1.1.1.1',dns2:''}))await page.getByLabel(en[`network.${key}`],{exact:true}).fill(value)
 await page.getByRole('button',{name:en['network.apply'],exact:true}).click();await confirm()
 await page.waitForFunction(()=>!document.querySelector('[role=dialog]'))
 assert.deepEqual(sent.at(-1),{op:'ip',interface:'ethernet',method:'static',ip:'192.0.2.108',prefix:24,gateway:'192.0.2.1',dns1:'1.1.1.1',dns2:''})
 await page.getByRole('button',{name:en['network.change_network'],exact:true}).click()
 await page.getByLabel(en['network.ssid'],{exact:true}).fill('Fixture "new"')
 await page.getByLabel(en['network.password'],{exact:true}).fill('fixture-only-pass')
 await page.getByRole('button',{name:en['network.connect'],exact:true}).click();await confirm()
 await page.waitForFunction(()=>!document.querySelector('[role=dialog]'))
 assert.equal(sent.at(-1).op,'wifi-connect')
 assert.equal(sent.at(-1).password,'fixture-only-pass')
 assert.equal(await page.getByLabel(en['network.password'],{exact:true}).count(),0)
 await page.getByRole('button',{name:en['network.change_network'],exact:true}).click()
 await page.getByRole('button',{name:'Saved fixture',exact:true}).click();await confirm()
 await page.waitForFunction(()=>!document.querySelector('[role=dialog]'))
 assert.deepEqual(sent.at(-1),{op:'wifi-saved',network_id:2})
 pending={phase:'pending',seconds:100,interface:'ethernet',ip:'192.0.2.107',token:'a'.repeat(32),origin:'http://192.0.2.107/#settings/connection',previous_origin:'http://192.0.2.103/#settings/connection'}
 await page.getByRole('button',{name:en['network.refresh'],exact:true}).click()
 holdConfirm=true
 await page.getByRole('button',{name:en['network.keep_connection'],exact:true}).click()
 const waiting=page.getByRole('button',{name:en['network.action_pending'],exact:true})
 await waiting.waitFor()
 assert.equal(await waiting.isDisabled(),true)
 assert.equal(await waiting.getAttribute('aria-busy'),'true')
 assert.equal(await waiting.evaluate(el=>getComputedStyle(el).cursor),'wait')
 const confirms=sent.filter(x=>x.op==='confirm').length
 const position=await waiting.boundingBox()
 await page.mouse.click(position.x+position.width/2,position.y+position.height/2)
 assert.equal(sent.filter(x=>x.op==='confirm').length,confirms)
 holdConfirm=false;releaseConfirm()
 await waiting.waitFor({state:'detached'})
 assert.equal(sent.at(-1).op,'confirm')
 const savedNotice=page.getByRole('status').filter({hasText:en['network.confirmation_saved']})
 await savedNotice.waitFor()
 await page.waitForTimeout(1200)
 assert.equal(await savedNotice.isVisible(),true)
 pending={phase:'pending',seconds:100,interface:'ethernet',ip:'192.0.2.107',token:'b'.repeat(32),origin:'http://192.0.2.107/#settings/connection',previous_origin:'http://192.0.2.103/#settings/connection'}
 await page.getByRole('button',{name:en['network.refresh'],exact:true}).click()
 failConfirm=true
 await page.getByRole('button',{name:en['network.keep_connection'],exact:true}).click()
 await page.getByRole('alert').waitFor()
 assert.equal(await savedNotice.count(),0)
 failConfirm=false;pending=null
 await page.getByText(en['network.show_diagnostics'],{exact:true}).click()
 await page.getByRole('textbox',{name:en['network.diagnostics'],exact:true}).waitFor()
 assert(!await page.getByRole('textbox',{name:en['network.diagnostics']}).inputValue().then(x=>x.includes('fixture-only-pass')))
 const shots=[]
 for(language of ['en','it','fr','es','pl','cs','hu','ru','zh'])for(const width of [320,390,768,1440]) {
  await page.setViewportSize({width,height:1000});await page.reload()
  const tr=await dict(language)
  await page.getByRole('button',{name:tr['network.change_network'],exact:true}).waitFor()
  await page.waitForTimeout(350)
  const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1)
  assert.equal(overflow,false,`${language} ${width} page overflows`)
  if(language==='it' && [390,1440].includes(width)) {
   const file=`connections-${language}-${width}.png`;await page.screenshot({path:path.join(out,file),fullPage:true});shots.push(file)
  }
 }
 assert.deepEqual(errors,[])
 await writeFile(path.join(out,'ui-validation.json'),JSON.stringify({actual_buttons_and_dialogs:true,no_lan_code_for_network_only:true,guard_header:true,cancel_no_mutation:true,static_ip:true,wifi_scan_and_connect:true,native_saved_profile:true,pending_confirmation:true,no_trial_or_unconditional_timer:true,nine_languages_four_widths:true,shots,errors},null,2)+'\n')
 console.log('PASS: actual Connections controls, confirmation, Wi-Fi/IP forms, credential handling and nine languages at four widths')
} finally {await browser.close()}
