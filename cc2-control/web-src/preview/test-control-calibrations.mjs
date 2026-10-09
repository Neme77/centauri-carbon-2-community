import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, machine = 1, homed = 'xyz'
let status = { busy: false, command: '', output: '', completed: false, success: false }
const commands = []
try {
 await server.listen()
 browser = await chromium.launch({headless:true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args:['--no-sandbox'] } : {})})
 const page = await browser.newPage({ viewport:{width:1440,height:1000} })
 await page.route('**/api/preferences', r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.route('**/api/printer', r=>r.fulfill({json:{connected:true,last_message_age:0,machine:{status:machine},motion:{homed_axes:homed},print:{state:machine===1?'idle':'printing'},extruder:{temperature:25,target:0},heater_bed:{temperature:25,target:0},tuning:{speed_percent:100,flow_percent:100}}}))
 await page.route('**/api/pid', r=>r.fulfill({json:{busy:false,ready:false}}))
 await page.route('**/api/console', r=>r.fulfill({json:status}))
 await page.route('**/api/console/command', r=>{commands.push(r.request().postData());status={busy:true,command:'SHAPER_CALIBRATE',output:'Measuring...',completed:false,success:false};return r.fulfill({status:202,json:{accepted:true}})})
 await page.goto(`${server.resolvedUrls.local[0]}#control`)
 const row=page.locator('.cc2-control-calibrations'), start=row.getByRole('button',{name:'Calibrate vibrations',exact:true})
 await page.waitForFunction(()=>!document.querySelector('.cc2-shaper button')?.disabled)
 const boxes=await Promise.all(['.cc2-shaper','.cc2-pid','.cc2-tuning'].map(s=>row.locator(s).boundingBox()))
 assert.ok(boxes[0].x<boxes[1].x && boxes[1].x<boxes[2].x)
 assert.ok(boxes.every(b=>Math.abs(b.y+b.height-boxes[0].y-boxes[0].height)<2),'desktop card bottoms align')
 await start.click();await page.getByRole('button',{name:'Cancel',exact:true}).click();assert.equal(commands.length,0)
 await start.click();await page.getByRole('button',{name:'Confirm',exact:true}).click()
 await page.waitForFunction(()=>document.querySelector('.cc2-shaper').textContent.includes('Measuring...'))
 assert.deepEqual(commands,['SHAPER_CALIBRATE']);assert.ok(await start.isDisabled())
 status={busy:false,command:'SHAPER_CALIBRATE',output:'Suggested shaper X',completed:true,success:true}
 await page.waitForFunction(()=>document.querySelector('.cc2-shaper').textContent.includes('Suggested shaper X'))
 homed='';await page.waitForFunction(()=>document.querySelector('.cc2-shaper button').disabled)
 machine=2;await page.waitForFunction(()=>document.querySelector('.cc2-shaper').textContent.includes('Available when idle'))
 for(const width of [390,768]){
  await page.setViewportSize({width,height:1000});await page.waitForTimeout(100)
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'no horizontal overflow')
  const b=await Promise.all(['.cc2-shaper','.cc2-pid','.cc2-tuning'].map(s=>row.locator(s).boundingBox()))
  assert.ok(b[0].y<b[1].y && b[1].y<b[2].y,'narrow row stacks in order')
 }
 console.log('PASS calibration placement desktop/mobile, confirmation, busy/homing/idle guards and output')
} finally {await browser?.close();await server.close()}
