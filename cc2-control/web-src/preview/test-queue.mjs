import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// Print queue against the preview backend: the page with its next-job card, pre-flight check, copies,
// order and policy; the Files chip; the print dialog's "Add to queue" choice; the Dashboard's quick start.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = [], posts = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.method() !== 'POST' || !r.url().includes('/api/queue/')) return
    assert.equal(r.headers()['x-cc2-request'], '1', 'queue changes carry the mutation marker')
    posts.push([new URL(r.url()).pathname, r.postData() || ''])
  })
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  // The file's filaments and the saved meshes come from the printer; the preview answers for it here.
  await page.route('**/api/gcode-files/inspect', r => r.fulfill({ json: { tools: [0], filaments: [{ tool: 0, color: '#1a1a1a', material: 'PLA' }], bed_temperature: 60, nozzle_diameter: 0.4 } }))
  await page.route('**/api/canvas', r => r.fulfill({ json: { available: false } }))
  await page.route('**/api/mesh', r => r.fulfill({ json: { result: { status: { bed_mesh: { profiles: { default: {}, default1: {} } } } } } }))
  await page.goto(`${origin}/#queue`)
  await Promise.all([page.waitForEvent('load'), page.selectOption('#preview-scene', 'idle')])
  await page.getByRole('heading', { name: 'Print queue' }).waitFor()
  const lastPost = () => posts.at(-1)

  // The head of the queue, its copy counter and the pre-flight check.
  const hero = page.locator('[data-queue-hero="next"]')
  await hero.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).waitFor()
  await hero.getByText('Copy 3 of 5', { exact: true }).waitFor()
  await hero.locator('[data-preflight] li').nth(2).waitFor()
  assert.ok((await hero.locator('[data-preflight] li').count()) >= 4, 'file, feed, mesh and printer are checked')
  await hero.locator('[data-preflight] li[data-tone="ok"]').first().waitFor()
  const rows = page.locator('[data-queue-item]')
  await rows.nth(3).waitFor()
  assert.equal(await rows.count(), 4, 'finished jobs are not among the open ones')
  await rows.nth(3).getByText('File missing', { exact: true }).waitFor()
  assert.equal(await page.locator('[data-queue-count]').textContent(), '4', 'the menu counts the open jobs')
  await page.locator('[data-queue-totals]').getByText('4', { exact: true }).first().waitFor()

  // Start: one POST, the toast names the copy, the counter moves on.
  await hero.locator('[data-queue-start]').click()
  await page.getByText('Copy 3 of 5 requested: CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/start', '1'])
  await hero.getByText('Copy 4 of 5', { exact: true }).waitFor()

  // Copies: the stepper of the second job.
  const second = rows.nth(1)
  await second.getByRole('button', { name: 'More copies', exact: true }).click()
  await second.locator('[data-queue-copies]').getByText('3', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/copies', '2\n3'])

  // Order: the second job moves below the third.
  await second.getByRole('button', { name: 'Move down', exact: true }).click()
  await rows.nth(1).getByText('CC2_Preview_Phone_stand.gcode', { exact: true }).waitFor()
  assert.deepEqual(lastPost(), ['/api/queue/reorder', '2\n2'])
  await rows.nth(2).getByText('CC2_Preview_Gear_housing_PETG.gcode', { exact: true }).waitFor()

  // Remove asks first.
  await rows.nth(3).locator('[data-queue-remove]').click()
  const dialog = page.getByRole('dialog')
  await dialog.getByText('Remove CC2_Preview_Vase_missing.gcode from the queue?', { exact: true }).waitFor()
  await dialog.getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('[data-queue-item]').length === 3)
  assert.deepEqual(lastPost(), ['/api/queue/remove', '4'])

  // Policy: the automatic start is a saved choice, the delay a number.
  const policy = page.locator('[data-queue-policy]')
  await policy.getByRole('radio', { name: 'Start the next copy automatically', exact: false }).check()
  await page.waitForFunction(() => document.querySelector('[data-queue-policy] input[type=radio]:nth-of-type(1)') !== null)
  assert.deepEqual(lastPost(), ['/api/queue/policy', 'auto\n60\n0'])
  await policy.getByRole('switch').click()
  await page.waitForFunction(() => document.querySelector('[data-queue-policy] [role=switch]')?.getAttribute('aria-checked') === 'true')
  assert.deepEqual(lastPost(), ['/api/queue/policy', 'auto\n60\n1'])

  // Finished jobs are folded away and can be cleared.
  await page.getByRole('button', { name: 'Finished (1)', exact: true }).click()
  await page.locator('[data-queue-done="5"]').waitFor()
  await page.getByRole('button', { name: 'Clear finished', exact: true }).click()
  await page.waitForFunction(() => !document.querySelector('[data-queue-done]'))
  assert.deepEqual(lastPost(), ['/api/queue/clear-done', ''])

  // Files: the queued file carries its chip; Print opens the dialog with the queue choice.
  await page.goto(`${origin}/#files`)
  const buddha = page.locator('.cc2-file-row').filter({ hasText: 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode' })
  await buddha.locator('[data-queued-chip]').getByText('In queue · 2', { exact: true }).waitFor()
  await buddha.getByRole('button', { name: 'Print', exact: true }).click()
  const print = page.getByRole('dialog')
  await print.locator('[data-print-launch]').waitFor()
  assert.equal(await print.locator('[data-print-submit]').textContent(), 'Start print')
  await print.locator('[data-launch="queue"]').click()
  await print.locator('[data-print-copies]').waitFor()
  await print.getByRole('button', { name: 'More copies', exact: true }).click()
  await print.getByRole('button', { name: 'More copies', exact: true }).click()
  assert.equal(await print.locator('[data-print-copies]').inputValue(), '3')
  assert.equal(await print.locator('[data-print-submit]').textContent(), 'Add to queue ×3')
  await print.getByRole('radio', { name: 'Next', exact: true }).check()
  await print.locator('[data-print-submit]').click()
  await page.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode added to the queue.', { exact: true }).waitFor()
  const add = posts.find(p => p[0] === '/api/queue/add')
  assert.ok(add, 'the dialog adds through /api/queue/add')
  const lines = add[1].split('\n')
  assert.equal(lines[0], 'internal')
  assert.equal(lines[1], 'CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode')
  assert.equal(lines[8], '3', 'three copies')
  assert.equal(lines[9], 'next', 'queued as the next job')
  assert.equal(lines[10], '1547', 'the slicer estimate travels with the job')
  await page.waitForFunction(() => !document.querySelector('[role=dialog]'))
  await buddha.locator('[data-queued-chip]').getByText('In queue · 5', { exact: true }).waitFor()

  // Dashboard: the quick start for the next copy while the printer is idle.
  await page.goto(`${origin}/#dashboard`)
  const quick = page.locator('[data-dashboard-queue]')
  await quick.getByText('Next in queue', { exact: true }).waitFor()
  await quick.getByText('CC2_Preview_Buddha_PLA_0.2mm_25m47s.gcode', { exact: true }).first().waitFor()
  await quick.locator('[data-dashboard-queue-start]').click()
  await page.waitForFunction(() => Array.from(document.querySelectorAll('*')).some(n => n.childElementCount === 0 && /Copy 1 of 3 requested/.test(n.textContent || '')))
  assert.equal(lastPost()[0], '/api/queue/start')

  assert.deepEqual(errors, [])
  console.log('PASS: print queue page, Files chip, dialog choice and Dashboard quick start')
} finally {
  await browser?.close()
  await server.close()
}
