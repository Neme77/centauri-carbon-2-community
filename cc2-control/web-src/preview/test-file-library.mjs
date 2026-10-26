import assert from 'node:assert/strict'
import { readFile, writeFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

const demo = await fixture()
const html = fileURLToPath(demo.url)
await writeFile(html, (await readFile(html, 'utf8')).replace('</head>', `<script>
window.__CC2_PREVIEW.preferences.language = 'en';
window.__libraryRequests = []; window.__uploads = []; window.__repeatCursor = false;
const libraryFiles = Array.from({length:300}, (_,i) => ({path:'part-'+String(300-i).padStart(3,'0')+'.gcode',size:100,modified:1700000300-i}));
const libraryGroup = start => {
  const files = libraryFiles.slice(start,start+128), more = start+128<libraryFiles.length;
  const last = files.at(-1);
  return {files,count:files.length,total:300,available:true,truncated:more,scan_truncated:false,has_more:more,next:more?{modified:String(last.modified),path:last.path}:null};
};
const originalFetch = window.fetch;
window.fetch = (input,options={}) => {
  const url = typeof input === 'string' ? input : input.url;
  if(url.includes('/api/gcode-files') && !url.includes('/upload')) {
    window.__libraryRequests.push(url);
    const parsed = new URL(url,'http://fixture');
    let group = libraryGroup(0);
    if(parsed.pathname.endsWith('/page') && !window.__repeatCursor) {
      const index = libraryFiles.findIndex(f=>f.path===parsed.searchParams.get('file') && String(f.modified)===parsed.searchParams.get('before'));
      if(index<0) throw Error('bad cursor');
      group = libraryGroup(index+1);
    }
    return Promise.resolve(new Response(JSON.stringify({internal:group,usb:{files:[]}}),{headers:{'Content-Type':'application/json'}}));
  }
  return originalFetch(input,options);
};
window.XMLHttpRequest = class {
  upload={}; open(method,url){this.method=method;this.url=url;} setRequestHeader(){}
  send(file){window.__uploads.push({size:file.size,timeout:this.timeout,url:this.url});this.status=201;this.responseText='{}';queueMicrotask(()=>this.onload());}
};
</script></head>`))
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(demo.url + '#files')
  const rows = page.locator('.cc2-file-row')
  await page.getByText('Page 1 of 20 · 300 items', { exact: true }).waitFor()
  assert.equal(await rows.count(), 15)
  assert.equal((await page.evaluate(() => window.__libraryRequests)).length, 3)
  await page.getByRole('button', { name: 'Next', exact: true }).click()
  await page.getByText('Page 2 of 20 · 300 items', { exact: true }).waitFor()
  assert.equal((await page.evaluate(() => window.__libraryRequests)).length, 3, 'local page navigation adds no requests')
  await page.getByPlaceholder('Search files').fill('part-001')
  await rows.getByRole('button', { name: 'part-001.gcode', exact: true }).waitFor()
  assert.equal(await rows.count(), 1, 'oldest file is searchable')
  await page.getByPlaceholder('Search files').fill('')
  for (const mib of [129, 450, 512]) {
    await page.locator('input[type=file][accept=".gcode"]').evaluate((input, size) => {
      const chunk = new Blob([new Uint8Array(1024*1024)])
      const data = new DataTransfer()
      data.items.add(new File(Array(size).fill(chunk), 'large.gcode'))
      input.files = data.files
      input.dispatchEvent(new Event('change', { bubbles: true }))
    }, mib)
    await page.getByRole('button', { name: 'Upload G-code', exact: true }).click()
    await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
    await page.waitForFunction(size => window.__uploads.some(u => u.size === size*1024*1024), mib)
    assert.equal((await page.evaluate(() => window.__uploads.at(-1))).timeout, 1830000)
    await page.getByRole('button', { name: 'Upload G-code', exact: true }).waitFor({state:'visible'})
    await page.waitForFunction(() => !document.querySelector('input[accept=".gcode"]').disabled)
  }
  await page.locator('input[type=file][accept=".gcode"]').evaluate(input => {
    const chunk = new Blob([new Uint8Array(1024*1024)])
    const data = new DataTransfer();data.items.add(new File(Array(513).fill(chunk),'oversized.gcode'))
    input.files=data.files;input.dispatchEvent(new Event('change',{bubbles:true}))
  })
  await page.getByRole('button', { name: 'Upload G-code', exact: true }).click()
  await page.getByText('Choose a valid .gcode file between 1 byte and 512 MiB.', {exact:true}).waitFor()
  assert.equal((await page.evaluate(() => window.__uploads)).length, 3)
  for (const width of [1440,390]) {
    await page.setViewportSize({width,height:1000})
    await page.waitForTimeout(300)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `no overflow at ${width}`)
  }
  await page.evaluate(() => {window.__repeatCursor=true;window.__libraryRequests=[]})
  await page.getByRole('button', { name: 'Refresh', exact: true }).click()
  await page.waitForFunction(() => window.__libraryRequests.length===2)
  await page.waitForTimeout(200)
  assert.equal((await page.evaluate(() => window.__libraryRequests)).length, 2, 'repeated cursor refuses an unbounded loop')
  assert.deepEqual(errors, [])
  console.log('PASS: 300-file library, oldest-file search, bounded cursor requests, 129/450/512 MiB UI admission, 513 MiB refusal, mobile and no repeated-cursor loop (XHR body mocked)')
} finally {
  await browser.close()
  await demo.cleanup()
}
