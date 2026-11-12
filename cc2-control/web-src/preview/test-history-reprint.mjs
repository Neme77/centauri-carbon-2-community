// History prints a job again only when its file is known for certain: one file of the job's name on the printer
// (any storage, any folder), written no later than the job began (30 s for a file copied as the job starts).
// Several files of that name, a file written after the job and a file that is gone leave the button disabled
// and say why; none of them shows a picture as if it were the job's.
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
  const t0 = 1700000000
  const internal = [
    { path: 'unique.gcode', size: 100, modified: t0 },
    { path: 'twice.gcode', size: 100, modified: t0 },
    { path: 'changed.gcode', size: 100, modified: t0 + 90000 }, // written a day after its job
    { path: 'copied.gcode', size: 100, modified: t0 + 710 }, // copied for its job, 10 s after it began
    { path: 'CC2_USB_1a2b3c4d_stick.gcode', size: 100, modified: t0 + 790 }, // CC2 Control's copy of a USB file
  ]
  const usb = [
    { path: 'models/twice.gcode', size: 100, modified: t0 - 1000 }, // the same name in a folder of the stick
    { path: 'stick.gcode', size: 100, modified: t0 - 1000 },
  ]
  const task = (id, name, begin) => ({ task_id: String(id), task_name: name, begin_time: begin, end_time: begin + 60, task_status: 1, time_lapse_video_status: 0 })
  const tasks = [
    task(1, 'unique.gcode', t0 + 500),
    task(2, 'twice.gcode', t0 + 600),
    task(3, 'changed.gcode', t0 + 650),
    task(4, 'gone.gcode', t0 + 400),
    task(5, 'copied.gcode', t0 + 700),
    task(6, 'CC2_USB_1a2b3c4d_stick.gcode', t0 + 800),
  ]
  const pictures = []
  const writes = []
  page.on('request', r => {
    const where = new URL(r.url()).pathname
    if (r.method() === 'POST' && !['/api/gcode-files/thumbnail', '/api/gcode-files/inspect', '/api/history/refresh', '/api/app-update/catalog'].includes(where)) writes.push(where)
  })
  await page.route('**/api/gcode-files/thumbnail', r => {
    pictures.push(r.request().postData().split('\n')[1])
    return r.fulfill({ contentType: 'image/svg+xml', body: '<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"><rect width="8" height="8" fill="#4caf50"/></svg>' })
  })
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0] } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {} } } } } } }))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/gcode-files', r => r.fulfill({ json: { internal: { files: internal, space: null }, usb: { files: usb, space: null } } }))
  await page.route('**/api/history', r => r.fulfill({ json: { available: true, pending: false, error_code: 0, reply: { result: { history_task_list: tasks } } } }))
  await page.route('**/api/history/refresh', r => r.fulfill({ json: { accepted: true } }))
  await page.route('**/api/printer', r => r.fulfill({ json: { connected: true, machine: { status: 1, status_name: 'Idle' }, print: { enabled: false }, extruder: { temperature: 25, target: 0 }, heater_bed: { temperature: 25, target: 0 }, tuning: {}, motion: {}, hardware: {} } }))
  await page.goto(server.resolvedUrls.local[0] + '#history')
  const job = id => page.locator(`[data-history-task="${id}"]`)
  const button = id => job(id).locator('[data-history-print]')
  await job(1).locator('[data-file-picture="shown"]').waitFor()
  await page.waitForFunction(() => document.querySelectorAll('[data-history-task] [data-file-picture="shown"]').length === 3)
  const state = async id => ({
    enabled: await button(id).isEnabled(),
    title: await button(id).getAttribute('title'),
    why: (await job(id).locator('[data-history-why]').count()) ? await job(id).locator('[data-history-why]').getAttribute('data-history-why') : '',
    picture: await job(id).locator('[data-file-picture]').getAttribute('data-file-picture'),
  })
  // A unique file written before its job: printed again, with its picture.
  assert.deepEqual(await state(1), { enabled: true, title: 'Print', why: '', picture: 'shown' })
  // The same name in the internal memory and in a folder of the stick: which one is not known.
  assert.deepEqual(await state(2), {
    enabled: false,
    title: 'Files of this name on the printer: 2. It is not certain which one this job printed; print the right one from Files.',
    why: 'several',
    picture: 'none',
  })
  assert.equal((await job(2).locator('[data-history-why]').textContent()).trim(), await button(2).getAttribute('title'), 'the row says it too')
  // A file of the job's name written after the job: not presented as the job's file.
  assert.deepEqual(await state(3), {
    enabled: false,
    title: 'This file was changed after the job, so it may not be the one the job printed. Print it from Files if it is the one you want.',
    why: 'changed',
    picture: 'none',
  })
  // A file that is gone: the button says so, the row does not.
  assert.deepEqual(await state(4), { enabled: false, title: 'This file is no longer on the printer.', why: '', picture: 'none' })
  // Copied for its job as the job began (a USB print started on the touchscreen), and CC2 Control's own copy of a
  // USB file, found by its exact name: both are the job's.
  assert.deepEqual(await state(5), { enabled: true, title: 'Print', why: '', picture: 'shown' })
  assert.deepEqual(await state(6), { enabled: true, title: 'Print', why: '', picture: 'shown' })
  assert.deepEqual(pictures.sort(), ['CC2_USB_1a2b3c4d_stick.gcode', 'copied.gcode', 'unique.gcode'], 'no picture is asked for a file that may not be the job\'s')
  // The print dialog of the Files page, for the job's own file; nothing is started.
  for (const [id, name] of [[1, 'unique.gcode'], [6, 'CC2_USB_1a2b3c4d_stick.gcode']]) {
    await button(id).click()
    const dialog = page.getByRole('dialog')
    await dialog.getByRole('button', { name: 'Start print', exact: true }).waitFor()
    await dialog.getByText(name, { exact: true }).first().waitFor()
    await page.keyboard.press('Escape')
    await dialog.waitFor({ state: 'hidden' })
  }
  for (const width of [1440, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(300)
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `no sideways scroll at ${width}`)
  }
  assert.deepEqual(writes, [], 'nothing was written')
  assert.deepEqual(errors, [])
  console.log('PASS: History prints again only a file known for certain: a unique file, one copied as its job began and CC2 Control\'s USB copy can; several files of the name, a file written after the job and a file that is gone cannot, say why and show no picture; desktop and phone')
} finally {
  await browser?.close()
  await server.close()
}
