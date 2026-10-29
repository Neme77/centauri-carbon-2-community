import assert from 'node:assert/strict'
import {readFile,writeFile,mkdir} from 'node:fs/promises'
import path from 'node:path'
import {createRequire} from 'node:module'
const app=path.resolve(process.argv[2]),output=path.resolve(process.argv[3])
const require=createRequire(path.join(app,'web-src/package.json'))
const {chromium}=require('playwright')
const en=JSON.parse(await readFile(path.join(app,'web-src/public/locales/en.json'),'utf8'))
const html=await readFile(path.join(app,'web/index.html'),'utf8')
const browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH,args:['--no-sandbox','--disable-gpu']}: {})})
const sent=[],errors=[]
let fail=false,busy=false,ready=true
try{
 const page=await browser.newPage({viewport:{width:1440,height:1000}})
 page.on('pageerror',error=>errors.push(error.message))
 await page.route('**/*',async route=>{
  const request=route.request(),url=new URL(request.url())
  assert.equal(url.hostname,'network-trial.test')
  if(url.pathname==='/')return route.fulfill({body:html,contentType:'text/html'})
  if(url.pathname==='/api/preferences')return route.fulfill({json:{language:'en',theme:'obsidian'}})
  if(url.pathname==='/api/setup')return route.fulfill({json:{configured:true,mqtt_connected:true,snapshot_received:true}})
  if(url.pathname==='/api/network/trial')return route.fulfill({json:{available:true,ready,busy,last_exit:0}})
  if(url.pathname==='/api/network/select'){
   sent.push({body:request.postData(),code:request.headers()['x-cc2-lan']})
   return route.fulfill({status:fail?409:202,json:fail?{error:'Fixture refused unsafe state'}:{accepted:true}})
  }
  if(url.pathname==='/api/network')return route.fulfill({json:{available:true,fresh:true,refreshing:false,age_ms:0,snapshot:{schema:1,wifi:{present:true,enabled:true,carrier:true,address:'192.0.2.103'},ethernet:{present:true,enabled:true,carrier:true,address:'192.0.2.107',link_mbps:1000},wireless:{control_available:true,state:'COMPLETED',ssid:'Fixture network'},route:{selected:'wifi'}}}})
  if(url.pathname==='/api/printer')return route.fulfill({json:{connected:true,machine:{status:1,status_name:'Idle'},print:{enabled:false},spools:{enabled:false,questions:[]},experiments:{},recovery:{available:false}}})
  return route.fulfill({json:{}})
 })
 await page.goto('http://network-trial.test/#settings/network')
 await page.getByRole('switch',{name:en['network.show_options']}).click()
 const prepare=page.getByRole('button',{name:en['network.prepare_trial'],exact:true})
 assert(await prepare.isDisabled(),'empty code enabled mutations')
 await page.locator('#network-lan-code').fill('fixture-code')
 await prepare.click()
 await page.getByRole('button',{name:en['common.cancel'],exact:true}).click()
 assert.equal(sent.length,0,'cancel still sent an action')
 for(const [role,label,body] of [['button','network.prepare_trial','start'],['radio','network.ethernet','ethernet'],['radio','network.wifi','wifi']]){
  await page.locator('#network-lan-code').fill('fixture-code')
  await page.getByRole(role,{name:en[label],exact:true}).click()
  await page.getByText(en[body==='start'?'network.confirm_prepare':body==='wifi'?'network.confirm_wifi':'network.confirm_ethernet'],{exact:true}).waitFor()
  await page.getByRole('button',{name:en['common.confirm'],exact:true}).click()
  await page.waitForFunction(n=>document.querySelector('#network-lan-code')?.value==='',null)
  assert.deepEqual(sent.at(-1),{body,code:'fixture-code'})
 }
 fail=true
 await page.locator('#network-lan-code').fill('fixture-code')
 await prepare.click();await page.getByRole('button',{name:en['common.confirm'],exact:true}).click()
 await page.getByRole('alert').filter({hasText:'Fixture refused unsafe state'}).waitFor()
 busy=true
 await page.getByRole('button',{name:en['network.refresh'],exact:true}).click()
 await page.waitForFunction(label=>[...document.querySelectorAll('.cc2-network button')].some(button=>button.textContent===label&&button.disabled),en['network.prepare_trial'])
 assert(await prepare.isDisabled(),'busy trial allowed overlapping selection')
 assert.equal(await page.getByRole('radio',{name:en['network.automatic'],exact:true}).count(),0)
 assert.deepEqual(errors,[])
 await mkdir(output,{recursive:true})
 const result={actual_manual_buttons:true,lan_header_and_fixed_bodies:true,cancel_sends_nothing:true,prepare_ethernet_wifi:true,failures_and_busy:true,no_automatic_mode:true,browser_errors:errors}
 await writeFile(path.join(output,'manual-validation.json'),JSON.stringify(result,null,2)+'\n')
 console.log(JSON.stringify(result))
}finally{await browser.close()}
