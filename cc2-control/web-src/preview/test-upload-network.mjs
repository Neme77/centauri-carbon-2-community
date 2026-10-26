import {chromium} from 'playwright'
import {spawn, execFile} from 'node:child_process'
import {promisify} from 'node:util'
import {fileURLToPath} from 'node:url'
import path from 'node:path'
import {createServer} from 'node:net'
import {mkdtemp,readFile,rm} from 'node:fs/promises'
import {tmpdir} from 'node:os'
import assert from 'node:assert/strict'
const dir=await mkdtemp(tmpdir()+'/cc2-upload-browser-')
const backend=fileURLToPath(new URL('../../',import.meta.url))
const binary=path.join(dir,'upload-server')
await promisify(execFile)('gcc',['-Os','-std=c11','-D_POSIX_C_SOURCE=200809L','-pthread',path.join(backend,'tests/upload_http_fixture.c'),...['mqtt','console','control','panda','uds'].map(n=>path.join(backend,'src',n+'.c')),'-lm','-o',binary])
const probe=createServer();await new Promise(r=>probe.listen(0,'127.0.0.1',r));const port=probe.address().port;await new Promise(r=>probe.close(r))
const proc=spawn(binary,[String(port),dir,path.join(backend,'web')])
const browser=await chromium.launch({headless:true,...(process.env.CC2_BROWSER_PATH?{executablePath:process.env.CC2_BROWSER_PATH,args:['--no-sandbox','--disable-gpu','--disable-software-rasterizer','--single-process','--no-zygote']}:{})})
try{
 const page=await browser.newPage();page.setDefaultTimeout(10000);await page.route('**/api/preferences',r=>r.fulfill({json:{language:'en',theme:'dark'}}))
 await page.goto(`http://127.0.0.1:${port}/#files`)
 const name='ECC2_0.4_3DBenchy_Elegoo PLA _0.2_41m50s(1).gcode'
 const buffer=process.env.CC2_UPLOAD_TEST_FILE?await readFile(process.env.CC2_UPLOAD_TEST_FILE):Buffer.alloc(4*1024*1024,'; test G-code\n')
 for(let i=0;i<2;i++){
  await page.locator('input[type=file]').setInputFiles({name,mimeType:'application/octet-stream',buffer})
  await page.getByRole('button',{name:'Upload G-code',exact:true}).click()
  const waiting=page.waitForResponse(r=>r.url().includes('/api/gcode-files/upload?'))
  await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
  const response=await waiting;assert.equal(response.status(),201)
  assert.deepEqual(await readFile(dir+'/'+name),buffer)
  await page.getByRole('button',{name,exact:true}).click()
  await page.getByRole('button',{name:'Delete',exact:true}).last().click()
  const deleted=page.waitForResponse(r=>r.url().endsWith('/api/gcode-files/delete'))
  await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
  assert.equal((await deleted).status(),200)
  console.log('PASS: browser upload / delete round',i+1)
 }
 // The same name again shows the server's refusal. The reset that hid it is
 // timing-dependent and does not occur on loopback; tests/test_file_ops.c
 // checks that the whole rejected body is read.
 for(const expected of [201,409]){
  await page.locator('input[type=file]').setInputFiles({name:'duplicate.gcode',mimeType:'application/octet-stream',buffer})
  await page.getByRole('button',{name:'Upload G-code',exact:true}).click()
  const waiting=page.waitForResponse(r=>r.url().includes('/api/gcode-files/upload?'))
  await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
  assert.equal((await waiting).status(),expected)
 }
 await page.getByText('Upload failed: File already exists').first().waitFor()
 console.log('PASS: same-name upload reports the conflict')
}finally{await browser.close();proc.kill();await rm(dir,{recursive:true,force:true})}
