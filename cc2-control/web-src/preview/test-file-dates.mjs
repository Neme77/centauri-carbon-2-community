import assert from 'node:assert/strict'
import { readFile, writeFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { fileModified, compareFileDates } from '../src/lib/file-date.ts'
import { fixture, launchOptions } from './fixture.mjs'

for (const value of [undefined, null, '', false, true, {}, -1, 0, 6432, 946684799, NaN, Infinity, 'bad', 8640000000001])
  assert.equal(fileModified(value), null, `unreliable timestamp: ${String(value)}`)
for (const value of [946684800, 1700000000, '1700000000', 8640000000000])
  assert.equal(fileModified(value), Number(value))
assert(compareFileDates({ path: 'z', modified: 0 }, { path: 'a', modified: 1700000000 }) > 0)
assert(compareFileDates({ path: 'b', modified: 9000 }, { path: 'a', modified: 1 }) > 0)

const demo = await fixture()
const html = fileURLToPath(demo.url)
await writeFile(html, (await readFile(html, 'utf8')).replace('</head>', `<script>
window.__CC2_PREVIEW.preferences.language = 'en';
window.__dateFiles = [
  {path:'bad-z.gcode',size:5,modified:6432},
  {path:'new.gcode',size:20,modified:1700000000},
  {path:'bad-a.gcode',size:7,modified:9000},
  {path:'old.gcode',size:30,modified:946684800},
  {path:'missing.gcode',size:2}
];
window.__dateWrites = [];
const dateFetch = window.fetch;
window.fetch = (input, options = {}) => {
  const url = typeof input === 'string' ? input : input.url;
  if (url.endsWith('/api/gcode-files')) {
    if (options.method && options.method !== 'GET') window.__dateWrites.push(options.method);
    return Promise.resolve(new Response(JSON.stringify({internal:{files:window.__dateFiles},usb:{files:[]}}), {headers:{'Content-Type':'application/json'}}));
  }
  return dateFetch(input, options);
};
</script></head>`))
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, timezoneId: 'Europe/Warsaw' })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(demo.url + '#files')
  const rows = page.locator('.cc2-file-row')
  await rows.nth(4).waitFor()
  const names = () => rows.evaluateAll(nodes => nodes.map(n => n.querySelector('button').textContent.trim()))
  assert.deepEqual(await names(), ['new.gcode', 'old.gcode', 'bad-a.gcode', 'bad-z.gcode', 'missing.gcode'])
  assert.equal(await rows.getByText('Date unavailable', { exact: true }).count(), 3)
  assert(!await rows.filter({ hasText: '1970' }).count())
  await page.locator('.cc2-file-date-note').waitFor()
  const expected = await page.evaluate(() => new Date(1700000000 * 1000).toLocaleString())
  await rows.filter({ hasText: 'new.gcode' }).getByText(expected, { exact: true }).waitFor()
  const sort = page.locator('select').filter({ has: page.locator('option[value="newest"]') })
  await sort.selectOption('name')
  assert.deepEqual(await names(), ['bad-a.gcode', 'bad-z.gcode', 'missing.gcode', 'new.gcode', 'old.gcode'])
  await sort.selectOption('size')
  assert.deepEqual(await names(), ['old.gcode', 'new.gcode', 'bad-a.gcode', 'bad-z.gcode', 'missing.gcode'])
  await page.getByPlaceholder('Search files').fill('bad-z')
  assert.equal(await rows.count(), 1)
  await page.getByPlaceholder('Search files').fill('')
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(200)
    if (process.env.CC2_SCREENSHOTS) {
      await mkdir(process.env.CC2_SCREENSHOTS, { recursive: true })
      await page.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, `file-dates-${width}.png`), fullPage: true })
    }
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `no overflow at ${width}`)
  }
  await page.evaluate(() => {
    window.__dateFiles = [{path:'new.gcode',size:20,modified:1700000000}];
  })
  await page.getByRole('button', { name: 'Refresh', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('.cc2-file-row').length === 1)
  assert.equal(await page.locator('.cc2-file-date-note').count(), 0)
  assert.deepEqual(await page.evaluate(() => window.__dateWrites), [])
  assert.deepEqual(errors, [])
  console.log('PASS: epoch/missing/malformed dates, valid dates, newest/name/size controls, search, refresh, mobile, no file writes')
} finally {
  await browser.close()
  await demo.cleanup()
}
