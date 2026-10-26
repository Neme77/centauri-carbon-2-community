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
  let tasks = Array.from({ length: 31 }, (_, i) => ({ task_id: String(i + 1), task_name: 'job-' + String(i + 1).padStart(3, '0'), begin_time: i + 1, end_time: i + 2, task_status: 1, time_lapse_video_status: 0 }))
  let lists = 0, historyRequests = 0
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
  const before = lists
  await next.click()
  await page.getByText('Page 2 of 3 · 31 items', { exact: true }).waitFor()
  assert.equal(await rows.count(), 15)
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
  console.log('PASS: storage labels, 15-row file/history pages, filtering, no page-driven requests, last-page deletion and mobile layouts')
} finally {
  await browser?.close()
  await server.close()
}
