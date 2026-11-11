import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
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
  let files = Array.from({ length: 31 }, (_, i) => ({ path: 'part-' + String(i + 1).padStart(3, '0') + '.gcode', size: 100, modified: i + 1 }))
  // The two newest jobs printed files that are still there (part-030 has no picture); the files of the others are gone.
  let tasks = Array.from({ length: 31 }, (_, i) => ({ task_id: String(i + 1), task_name: i >= 29 ? files[i - 1].path : 'job-' + String(i + 1).padStart(3, '0'), begin_time: i + 1, end_time: i + 2, task_status: 1, time_lapse_video_status: 0 }))
  let lists = 0, historyRequests = 0
  const pictures = []
  await page.route('**/api/gcode-files/thumbnail', async r => {
    const name = r.request().postData().split('\n')[1]
    pictures.push(name)
    await new Promise(done => setTimeout(done, 60))
    return name === 'part-002.gcode' || name === 'part-030.gcode'
      ? r.fulfill({ status: 404, json: { error: 'No embedded thumbnail' } })
      : r.fulfill({ contentType: 'image/svg+xml', body: '<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"><rect width="8" height="8" fill="#4caf50"/></svg>' })
  })
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0] } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/gcode-files', r => { lists++; return r.fulfill({ json: { internal: { files, space: { total_bytes: 104857600, available_bytes: 52428800 } }, usb: { files: [], space: null } } }) })
  await page.route('**/api/gcode-files/delete', r => { files = files.filter(f => f.path !== r.request().postData().split('\n')[1]); return r.fulfill({ json: { deleted: true } }) })
  await page.route('**/api/history', r => r.fulfill({ json: { available: true, pending: false, error_code: 0, reply: { result: { history_task_list: tasks } } } }))
  await page.route('**/api/history/refresh', r => { historyRequests++; return r.fulfill({ json: { accepted: true } }) })
  await page.route('**/api/history/delete', r => { const ids = r.request().postData().split('\n'); tasks = tasks.filter(t => !ids.includes(t.task_id)); return r.fulfill({ json: { accepted: true } }) })
  await page.route('**/api/printer', r => r.fulfill({ json: { connected: true, machine: { status: 1, status_name: 'Idle' }, print: { enabled: false }, extruder: { temperature: 25, target: 0 }, heater_bed: { temperature: 25, target: 0 }, tuning: {}, motion: {}, hardware: {} } }))
  const origin = server.resolvedUrls.local[0]
  await page.goto(origin + '#files')
  const rows = page.locator('.cc2-file-row')
  await rows.nth(14).waitFor()
  assert.equal(await rows.count(), 15)
  await page.getByText('50.0 MiB available of 100.0 MiB', { exact: true }).waitFor()
  await page.getByText('Storage space unavailable', { exact: true }).waitFor()
  const next = page.getByRole('button', { name: 'Next', exact: true })
  const previous = page.getByRole('button', { name: 'Previous', exact: true })
  // Every row shows the picture of its file, or the icon when the file has none: one request per file.
  await page.waitForFunction(() => document.querySelectorAll('.cc2-file-row [data-file-picture="shown"]').length === 14)
  assert.equal(await rows.nth(1).locator('[data-file-picture]').getAttribute('data-file-picture'), 'none', 'part-002 has no picture')
  assert.equal(await rows.locator('img').evaluateAll(all => all.every(i => i.complete && i.naturalWidth > 0 && i.getBoundingClientRect().width <= 48)), true)
  assert.deepEqual([pictures.length, new Set(pictures).size], [15, 15])
  const before = lists
  // Two pages on at once: the pictures of the page that was only passed are not asked for.
  await next.click()
  await next.click()
  await page.getByText('Page 3 of 3 · 31 items', { exact: true }).waitFor()
  assert.equal(await rows.count(), 1)
  await rows.locator('[data-file-picture="shown"]').waitFor()
  assert.ok(pictures.length <= 15 + 3 + 1, `the passed page asked for ${pictures.length - 16} pictures`)
  await previous.click()
  await page.getByText('Page 2 of 3 · 31 items', { exact: true }).waitFor()
  assert.equal(await rows.count(), 15)
  await page.waitForFunction(() => document.querySelectorAll('.cc2-file-row [data-file-picture="shown"]').length === 14)
  await previous.click()
  await page.getByText('Page 1 of 3 · 31 items', { exact: true }).waitFor()
  await page.waitForFunction(() => document.querySelectorAll('.cc2-file-row [data-file-picture="shown"]').length === 14)
  assert.deepEqual([pictures.length, new Set(pictures).size], [31, 31], 'a page shown again asks for nothing')
  // The details of a selected file show the same picture without another request.
  await rows.first().click()
  await page.getByRole('img', { name: 'G-code model preview', exact: true }).waitFor()
  assert.equal(pictures.length, 31)
  await next.click()
  await next.click()
  await page.getByText('Page 3 of 3 · 31 items', { exact: true }).waitFor()
  assert.equal(await rows.count(), 1)
  assert.equal(lists, before, 'page navigation must not fetch the list again')
  await page.getByPlaceholder('Search files').fill('part-031')
  await page.getByText('Page 1 of 1 · 1 items', { exact: true }).waitFor()
  await page.getByPlaceholder('Search files').fill('')
  await page.getByText('Page 1 of 3 · 31 items', { exact: true }).waitFor()
  await next.click(); await next.click()
  await rows.first().getByRole('button', { name: 'Delete', exact: true }).click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('Page 2 of 2 · 30 items', { exact: true }).waitFor()
  assert.equal(await rows.count(), 15)
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(400)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'files overflow at ' + width)
  }
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.evaluate(() => { location.hash = '#history' })
  const jobs = page.locator('.cc2-history-list button[aria-label^="Delete"]')
  await jobs.nth(14).waitFor()
  assert.equal(await jobs.count(), 15)
  // A job whose file is still on the printer shows its picture and can be printed again: the print dialog of
  // the Files page opens. A job whose file is gone shows the icon and says why it cannot.
  const job = id => page.locator(`[data-history-task="${id}"]`)
  await job(30).locator('[data-file-picture="shown"]').waitFor()
  assert.equal(await job(30).locator('[data-history-print]').isEnabled(), true)
  assert.equal(await job(31).locator('[data-file-picture]').getAttribute('data-file-picture'), 'none')
  assert.equal(await job(31).locator('[data-history-print]').isEnabled(), true)
  assert.equal(await job(29).locator('[data-file-picture]').getAttribute('data-file-picture'), 'none')
  assert.equal(await job(29).locator('[data-history-print]').isDisabled(), true)
  assert.equal(await job(29).locator('[data-history-print]').getAttribute('title'), 'This file is no longer on the printer.')
  assert.equal(await page.locator('[data-history-print]:not([disabled])').count(), 2)
  assert.deepEqual([pictures.length, new Set(pictures).size], [31, 31], 'the files were shown before: no picture is asked for twice, none for a file that is gone')
  await page.getByRole('button', { name: 'Print part-029.gcode', exact: true }).click()
  const printing = page.getByRole('dialog')
  await printing.getByRole('button', { name: 'Start print', exact: true }).waitFor()
  await printing.getByText('part-029.gcode').first().waitFor()
  await page.keyboard.press('Escape')
  await printing.waitFor({ state: 'hidden' })
  const requests = historyRequests
  await next.click(); await next.click()
  await page.getByText('Page 3 of 3 · 31 items', { exact: true }).waitFor()
  assert.equal(await jobs.count(), 1)
  assert.equal(historyRequests, requests, 'history pages must not request MQTT history')
  await jobs.first().click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.getByText('Page 2 of 2 · 30 items', { exact: true }).waitFor()
  assert.equal(await jobs.count(), 15)
  await previous.click()
  await page.getByText('Page 1 of 2 · 30 items', { exact: true }).waitFor()
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'history overflow at ' + width)
  }
  assert.deepEqual(errors, [])
  console.log('PASS: storage labels, 15-row file/history pages, filtering, no page-driven requests, last-page deletion and mobile layouts, a picture for every file and job, printing from History')
} finally {
  await browser?.close()
  await server.close()
}
