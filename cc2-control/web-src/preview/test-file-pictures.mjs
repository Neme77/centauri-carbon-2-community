// The pictures of the Files, History and Queue lists share one bounded cache (lib/pictures.ts): browsing a
// library larger than the cache keeps the number of live object URLs bounded, a URL is never revoked while a
// row shows it, an evicted picture is asked for again when its row comes back, and a file replaced under its
// name shows its new picture and frees the old one.
import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const KEEP = 48 // pictures kept for rows that are not shown, as in lib/pictures.ts
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  // Every object URL the page creates or revokes, to tell the live ones and the revoked ones a row still shows.
  await page.addInitScript(() => {
    const live = new Set(), revoked = new Set()
    const create = URL.createObjectURL.bind(URL), revoke = URL.revokeObjectURL.bind(URL)
    URL.createObjectURL = blob => { const url = create(blob); live.add(url); return url }
    URL.revokeObjectURL = url => { live.delete(url); revoked.add(url); revoke(url) }
    window.__pictures = { live, revoked }
  })
  const files = Array.from({ length: 120 }, (_, i) => ({ path: 'part-' + String(i + 1).padStart(3, '0') + '.gcode', size: 100, modified: 1700000000 + i }))
  const tasks = Array.from({ length: 40 }, (_, i) => ({ task_id: String(i + 1), task_name: files[i].path, begin_time: 1700001000 + i, end_time: 1700001100 + i, task_status: 1, time_lapse_video_status: 0 }))
  const asked = []
  const version = new Map()
  await page.route('**/api/gcode-files/thumbnail', async r => {
    const name = r.request().postData().split('\n')[1]
    asked.push(name)
    await new Promise(done => setTimeout(done, 15))
    const colour = version.get(name) ? '#e53935' : '#4caf50'
    return r.fulfill({ contentType: 'image/svg+xml', body: `<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"><rect width="8" height="8" fill="${colour}"/></svg>` })
  })
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0] } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/gcode-files', r => r.fulfill({ json: { internal: { files, space: { total_bytes: 104857600, available_bytes: 52428800 } }, usb: { files: [], space: null } } }))
  await page.route('**/api/history', r => r.fulfill({ json: { available: true, pending: false, error_code: 0, reply: { result: { history_task_list: tasks } } } }))
  await page.route('**/api/history/refresh', r => r.fulfill({ json: { accepted: true } }))

  const audit = async where => {
    const seen = await page.evaluate(() => {
      const shown = [...document.querySelectorAll('img')].filter(i => i.src.startsWith('blob:'))
      const { live, revoked } = window.__pictures
      return {
        shown: new Set(shown.map(i => i.src)).size,
        live: live.size,
        revokedInUse: shown.filter(i => revoked.has(i.src)).length,
        broken: shown.filter(i => i.complete && i.naturalWidth === 0).length,
      }
    })
    assert.equal(seen.revokedInUse, 0, `${where}: a shown picture uses a revoked URL`)
    assert.equal(seen.broken, 0, `${where}: a shown picture did not load`)
    assert.ok(seen.live <= KEEP + seen.shown, `${where}: ${seen.live} live URLs for ${seen.shown} shown pictures`)
    return seen
  }
  const rows = page.locator('.cc2-file-row')
  const filesPage = async n => {
    await page.getByText(`Page ${n} of 8 · 120 items`, { exact: true }).waitFor()
    await page.waitForFunction(() => document.querySelectorAll('.cc2-file-row [data-file-picture="shown"]').length === 15)
    return audit(`Files page ${n}`)
  }
  const next = page.getByRole('button', { name: 'Next', exact: true })
  const previous = page.getByRole('button', { name: 'Previous', exact: true })
  const origin = server.resolvedUrls.local[0]
  await page.goto(origin + '#files')
  await rows.nth(14).waitFor()
  await filesPage(1)
  // Every page of a library larger than the cache, one after the other: each picture is asked for once.
  for (let n = 2; n <= 8; n++) {
    await next.click()
    await filesPage(n)
  }
  assert.deepEqual([asked.length, new Set(asked).size], [120, 120])
  const last = await audit('Files, last page')
  assert.ok(last.live <= KEEP + 15, `the cache holds ${last.live} pictures`)
  // Back to the first page: the pictures of the pages seen long ago were dropped and are asked for again; the
  // ones seen last are still there.
  for (let n = 7; n >= 1; n--) {
    await previous.click()
    await filesPage(n)
  }
  const again = asked.slice(120)
  const firstPage = files.slice(105).map(f => f.path) // newest first: page 1 shows part-106 to part-120
  const lastPage = files.slice(0, 15).map(f => f.path)
  assert.ok(firstPage.every(name => again.includes(name)), 'the first page was dropped and asked for again')
  assert.ok(lastPage.every(name => !again.includes(name)), 'the pages seen last were kept')
  assert.equal(new Set(again).size, again.length, 'no picture is asked for twice on the way back')
  // A file replaced under its name: the row shows the new picture, and the old one is freed.
  const first = rows.first().locator('[data-file-picture] img')
  const oldUrl = await first.getAttribute('src')
  const before = asked.filter(name => name === files[119].path).length
  files[119] = { ...files[119], size: 200, modified: files[119].modified + 1000 }
  version.set(files[119].path, 2)
  await page.getByRole('button', { name: 'Refresh', exact: true }).click()
  await page.waitForFunction(old => {
    const img = document.querySelector('.cc2-file-row [data-file-picture] img')
    return img && img.src !== old && img.complete && img.naturalWidth > 0
  }, oldUrl)
  await page.waitForFunction(old => window.__pictures.revoked.has(old), oldUrl)
  assert.equal(asked.filter(name => name === files[119].path).length, before + 1, 'the new version is asked for once')
  await audit('Files after a replaced file')
  // History and Queue share the cache: the jobs whose files were shown take their pictures, the rest are asked for.
  await page.evaluate(() => { location.hash = '#history' })
  await page.locator('[data-history-task]').nth(14).waitFor()
  await page.waitForFunction(() => document.querySelectorAll('[data-history-task] [data-file-picture="shown"]').length === 15)
  await audit('History')
  await page.evaluate(() => { location.hash = '#queue' })
  await page.locator('[data-queue-rows] [data-queue-item]').first().waitFor()
  const jobs = await page.locator('[data-queue-rows] [data-queue-item]').count()
  await page.waitForFunction(n => document.querySelectorAll('[data-queue-rows] [data-file-picture="shown"]').length === n, jobs)
  await audit('Queue')
  await page.evaluate(() => { location.hash = '#files' })
  await filesPage(1)
  assert.deepEqual(errors, [])
  console.log(`PASS: one bounded picture cache for Files, History and Queue (${KEEP} kept beyond the shown rows): 120 files browsed with at most ${KEEP + 15} live URLs, no revoked or broken picture on screen, dropped pictures asked for again, a replaced file shows its new picture and frees the old one`)
} finally {
  await browser?.close()
  await server.close()
}
