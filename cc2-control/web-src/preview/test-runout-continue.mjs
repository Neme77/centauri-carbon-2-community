import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
// Continue from another Canvas slot after a runout (issue #284): the card on a runout pause, slot choice,
// confirmation (with the material warning), one request, the running state; and the "match material only" switch.
const server = await createServer({ root: new URL('..', import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser
const slots = [
  { slot: 0, status: 1, type: 'PA', color: '#FFFFFF', eligible: true, same_material: false },
  { slot: 1, status: 1, type: 'ASA', color: '#000000', eligible: true, same_material: true },
  { slot: 2, status: 0, type: 'ASA', color: '#696357', eligible: false, same_material: false },
  { slot: 3, status: 0, type: 'PETG', color: '#000000', eligible: false, same_material: false },
]
const state = {
  available: true, reason: 'none', match_material: false,
  job: { state: 'idle', slot: -1, from: -1, automatic: false, message: '', auto_note: '' },
  slot: 2, tools: [0], material: 'ASA', slots,
}
let printState = 'paused'
const posts = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async r => {
    const response = await r.fetch(); const data = await response.json()
    data.connected = true; data.print.state = printState
    return r.fulfill({ json: data })
  })
  await page.route('**/api/canvas', async r => {
    const response = await r.fetch(); const data = await response.json()
    return r.fulfill({ json: { ...data, auto_refill: true } })
  })
  await page.route('**/api/runout', r => r.fulfill({ json: printState === 'paused' ? state : { ...state, available: false, reason: 'not_paused' } }))
  await page.route('**/api/runout/*', r => {
    const what = r.request().url().split('/').pop(), body = r.request().postData()
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    posts.push([what, body])
    if (what === 'continue') {
      Object.assign(state, { available: false, reason: 'busy', job: { ...state.job, state: 'running', slot: Number(body.split('=')[1]), from: 2 } })
      return r.fulfill({ status: 202, json: { accepted: true, reason: 'none' } })
    }
    state.match_material = body === 'on'
    return r.fulfill({ json: { accepted: true } })
  })
  await page.goto(`${server.resolvedUrls.local[0]}#dashboard`)
  const card = page.locator('[data-runout-continue]'); await card.waitFor()
  assert.match(await card.textContent(), /Slot 3 \(ASA, T0\) is empty/)
  // Only loaded slots are offered; the same material is marked.
  const buttons = card.getByRole('button')
  assert.equal(await buttons.count(), 2)
  await card.getByRole('button', { name: /Continue from slot 2/ }).getByText('Same material').waitFor()
  await card.getByRole('button', { name: /Continue from slot 1/ }).getByText('Other material').waitFor()
  await mkdir(new URL('./screenshots/', import.meta.url), { recursive: true })
  await card.screenshot({ path: new URL('./screenshots/runout-continue.png', import.meta.url).pathname })
  // A different material asks with a warning; cancelling sends nothing.
  await card.getByRole('button', { name: /Continue from slot 1/ }).click()
  assert.match(await page.getByRole('dialog').textContent(), /labelled PA, not ASA/)
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.deepEqual(posts, [])
  // The same material: one request, then the card shows the reload.
  await card.getByRole('button', { name: /Continue from slot 2/ }).click()
  assert.match(await page.getByRole('dialog').textContent(), /Continue the print from slot 2\?/)
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await card.getByText('Loading slot 2 and resuming…').first().waitFor()
  assert.deepEqual(posts, [['continue', 'slot=1']])
  assert.equal(await card.getByRole('button').count(), 0, 'no second choice while it reloads')
  // The print runs again: the card goes away.
  printState = 'printing'
  await card.waitFor({ state: 'detached', timeout: 10000 })
  // Canvas page: the switch is there, enabled with auto refill on, and saves.
  await page.goto(`${server.resolvedUrls.local[0]}#canvas`)
  const match = page.locator('[data-match-material]'); await match.waitFor()
  await page.waitForFunction(() => !document.querySelector('[data-match-material]').disabled, null, { timeout: 10000 })
  assert.equal(await match.getAttribute('aria-pressed'), 'false')
  await match.click()
  await page.waitForFunction(() => document.querySelector('[data-match-material]').getAttribute('aria-pressed') === 'true')
  assert.deepEqual(posts.at(-1), ['match-material', 'on'])
  await page.locator('[data-canvas-settings]').screenshot({ path: new URL('./screenshots/runout-match-material.png', import.meta.url).pathname })
  assert.deepEqual(errors, [])
  console.log('PASS: runout card on a runout pause, loaded slots only, material warning, cancel/confirm, one request, reload state, card closes on resume; match-material switch')
} finally {
  await browser?.close()
  await server.close()
}
