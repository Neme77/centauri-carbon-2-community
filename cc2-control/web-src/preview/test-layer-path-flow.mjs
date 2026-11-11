// Layer path request flow against a backend that, like the real one, scans one
// uncached layer at a time, keeps scanning after the client gives up, and
// answers 503 while busy. The client cache starts cold.
import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":"en"}'))
  await page.goto(demo.url + '#job')
  await page.evaluate(() => {
    const state = window.__CC2_PREVIEW
    Object.assign(state, { status: 2, filename: 'flow.gcode', current: '', excluded: [], layer: 0, totalLayers: 227, nozzleX: 60 })
    state.objectRows = [{ name: 'Part', center: [128, 128] }]
    const scan = (state.scan = { busyUntil: 0, duration: 300, durations: {}, fail: {}, limit: {}, log: [] })
    const original = window.fetch
    const json = (body, status = 200) =>
      new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } })
    window.fetch = (url, opts = {}) => {
      const u = String(url)
      if (u.includes('/api/printer'))
        return original(url, opts).then(async r => {
          state.printerPolls = (state.printerPolls || 0) + 1
          const d = await r.json()
          d.print.filename = state.filename
          d.print.current_layer = state.layer
          d.print.total_layers = state.totalLayers
          d.motion = { ...d.motion, x: state.nozzleX, y: 128, z: state.layer * 0.2 }
          return json(d)
        })
      if (u.includes('/api/exclude-objects'))
        return Promise.resolve(
          json({ result: { status: { exclude_object: { objects: state.objectRows, current_object: state.current, excluded_objects: state.excluded } } } })
        )
      if (u.includes('/api/gcode-files/layer-path')) {
        const layer = Number(new URL(u, location.href).searchParams.get('layer'))
        const now = performance.now()
        if (scan.fail[layer]) {
          scan.log.push(`${layer}:404`)
          return Promise.resolve(json({ error: 'Layer is outside the file' }, 404))
        }
        if (scan.limit[layer]) {
          scan.log.push(`${layer}:413`)
          return Promise.resolve(json({ error: 'Layer path scan limit reached' }, 413))
        }
        if (now < scan.busyUntil) {
          scan.log.push(`${layer}:503`)
          return Promise.resolve(json({ error: 'Layer path busy; retry shortly' }, 503))
        }
        const duration = scan.durations[layer] ?? scan.duration
        scan.busyUntil = now + duration - 5 // the scan runs to its end even when the client aborts
        scan.log.push(`${layer}:200`)
        return new Promise((resolve, reject) => {
          const timer = setTimeout(
            () =>
              resolve(
                json({
                  storage: 'internal',
                  file: state.filename,
                  layer,
                  total_layers: state.totalLayers,
                  z: Math.round(layer * 20) / 100,
                  paths: [[[20, 20], [200, 20 + (layer % 50)]]],
                  point_count: 2,
                  truncated: false,
                })
              ),
            duration
          )
          opts.signal?.addEventListener('abort', () => {
            clearTimeout(timer)
            reject(new DOMException('Aborted', 'AbortError'))
          })
        })
      }
      return original(url, opts)
    }
  })

  const log = () => page.evaluate(() => [...window.__CC2_PREVIEW.scan.log])
  const clearLog = () => page.evaluate(() => (window.__CC2_PREVIEW.scan.log = []))
  const set = values => page.evaluate(v => Object.assign(window.__CC2_PREVIEW, v), values)
  const setScan = values => page.evaluate(v => Object.assign(window.__CC2_PREVIEW.scan, v), values)
  // The status line alone is not enough: it can show the layer before its path arrives.
  // Each mock layer ends at (200, 20 + layer % 50), so the drawn path identifies its layer.
  const waitLayer = n =>
    page.waitForFunction(
      n =>
        new RegExp(`Layer ${n} / 227`).test(document.querySelector('[data-layer-status]')?.textContent || '') &&
        Array.from(document.querySelectorAll('[data-layer-path="printed"], [data-layer-path="remain"]')).some(el =>
          (el.getAttribute('points') || '').split(' ').includes(`200,${256 - (20 + (n % 50))}`)
        ),
      n
    )
  const waitLog = entry => page.waitForFunction(e => window.__CC2_PREVIEW.scan.log.includes(e), entry)
  const dialog = page.getByRole('dialog')
  const unavailable = () => dialog.getByText('Layer path not available', { exact: true }).count()
  const open = page.getByRole('button', { name: 'Expand bed map', exact: true })
  const openLayerTab = async () => {
    await open.waitFor()
    await page.waitForFunction(
      () => !Array.from(document.querySelectorAll('button')).find(e => e.textContent.trim() === 'Expand bed map')?.disabled
    )
    await open.click()
    await dialog.getByRole('button', { name: 'Layer path', exact: true }).click()
  }

  // Preparing (layer 0): Live waits for layer 1 instead of failing, stepping away
  // lands on layer 1, and Live can be chosen again before the first layer starts.
  await page.waitForFunction(() => window.__CC2_PREVIEW.printerPolls >= 2)
  await openLayerTab()
  const waiting = () =>
    page.waitForFunction(() => document.querySelector('[data-layer-status]')?.textContent.includes('Waiting for layer 1'))
  await waiting()
  assert.equal(await unavailable(), 0, 'layer 0 waits for layer 1 instead of showing the path as unavailable')
  await dialog.getByRole('button', { name: 'Next layer' }).click()
  await waitLayer(1)
  const live = dialog.locator('[data-layer-live]')
  assert.equal(await live.isEnabled(), true, 'Live can be chosen again while the print prepares')
  await live.click()
  await waiting()
  assert.equal(await live.getAttribute('aria-pressed'), 'true')

  // Cold cache: layer 1, then the printer moves on to layer 2.
  await set({ layer: 1 })
  await waitLayer(1)
  assert.deepEqual(await log(), ['1:200'], 'layer 1 needs no previous layer')
  await set({ layer: 2 })
  await waitLayer(2)
  await page.waitForTimeout(600)
  assert.deepEqual(await log(), ['1:200', '2:200'], 'layer 1 -> 2 scans only layer 2; layer 1 comes from the client cache')
  assert.ok(await dialog.locator('[data-layer-path="prev"]').count(), 'layer 1 drawn underneath layer 2')

  // The current layer is shown even when the previous layer cannot be read.
  await clearLog()
  await setScan({ fail: { 9: true } })
  await set({ layer: 10 })
  await waitLayer(10)
  await waitLog('9:404')
  assert.deepEqual(await log(), ['10:200', '9:404'], 'current layer first, previous layer afterwards')
  assert.equal(await unavailable(), 0, 'a failed previous layer does not hide the current one')
  assert.equal(await dialog.locator('[data-layer-path="prev"]').count(), 0, 'no previous layer drawn when it failed')
  assert.ok(await dialog.locator('[data-layer-path="printed"], [data-layer-path="remain"]').count(), 'current layer drawn')

  // Busy backend: another scan is still running. The current layer waits and retries.
  await clearLog()
  await page.evaluate(() => (window.__CC2_PREVIEW.scan.busyUntil = performance.now() + 2500))
  await set({ layer: 20 })
  await waitLog('20:503')
  assert.ok(await dialog.getByText('Loading layer…', { exact: true }).count(), 'busy is shown as loading')
  assert.equal(await unavailable(), 0, 'busy is not shown as unavailable')
  await waitLayer(20)
  const busy = await log()
  assert.ok(busy.filter(e => e === '20:503').length >= 1 && busy.includes('20:200'), `busy current layer retried: ${busy}`)
  assert.ok(busy.every(e => e.startsWith('20:') || e.startsWith('19:')), `only the latest layer is retried: ${busy}`)
  await page.waitForTimeout(800)

  // Rapid manual stepping requests only the layer it settles on.
  await clearLog()
  const next = dialog.getByRole('button', { name: 'Next layer' })
  for (let i = 0; i < 5; i++) await next.click()
  await waitLayer(25)
  await waitLog('24:200')
  await page.waitForTimeout(600)
  assert.deepEqual(await log(), ['25:200', '24:200'], 'rapid stepping 20 -> 25 scans 25 and then its previous layer only')

  // Back to Live: layer 20 is cached, so nothing is scanned.
  await clearLog()
  await dialog.getByRole('button', { name: 'Live', exact: true }).click()
  await waitLayer(20)
  await page.waitForTimeout(800)
  assert.deepEqual(await log(), [], 'switching back to Live uses the cached layer')

  // Printer updates while the layer stays the same move the nozzle without a layer fetch.
  const nozzleLeft = () => dialog.locator('[data-layer-nozzle]').evaluate(el => el.style.left)
  const before = await nozzleLeft()
  await set({ nozzleX: 180 })
  await page.waitForFunction(b => document.querySelector('[data-layer-nozzle]')?.style.left !== b, before)
  await set({ nozzleX: 90 })
  await page.waitForFunction(
    () => Math.abs(parseFloat(document.querySelector('[data-layer-nozzle]')?.style.left) - (90 / 256) * 100) < 0.01
  )
  await page.waitForTimeout(800)
  assert.deepEqual(await log(), [], 'nozzle moves without another layer request')

  // The Objects tab names only the layer being printed, not the one viewed in Layer path.
  await dialog.getByRole('button', { name: 'Previous layer' }).click()
  await waitLayer(19)
  await dialog.getByRole('button', { name: 'Objects', exact: true }).click()
  await page.waitForFunction(() => document.querySelector('[data-layer-status]')?.textContent === 'Printing layer 20 / 227')
  await dialog.getByRole('button', { name: 'Layer path', exact: true }).click()
  await waitLayer(19)
  await dialog.getByRole('button', { name: 'Live', exact: true }).click()
  await waitLayer(20)
  await clearLog()

  // A slow layer abandoned for another one never replaces it on screen.
  await clearLog()
  await setScan({ durations: { 30: 1500 } })
  await dialog.getByRole('button', { name: 'Previous layer' }).click() // 19, cached as the previous of 20
  for (let i = 0; i < 11; i++) await next.click() // settles on 30
  await waitLog('30:200')
  await next.click() // 31 while 30 is still scanning
  await waitLayer(31)
  await page.waitForTimeout(1800)
  assert.match(await dialog.locator('[data-layer-status]').innerText(), /Layer 31 \/ 227/, 'an obsolete response stays off screen')
  assert.equal(await unavailable(), 0)

  // Closing and reopening the dialog while a request is pending.
  await clearLog()
  await setScan({ durations: { 40: 1500 } })
  await dialog.getByRole('button', { name: 'Live', exact: true }).click()
  await set({ layer: 40 })
  await waitLog('40:200')
  await dialog.getByRole('button', { name: 'Close bed map', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('[role=dialog]').length === 0)
  await openLayerTab()
  await waitLayer(40)
  assert.equal(await unavailable(), 0, 'reopening during a pending scan waits for it instead of failing')
  const reopened = await log()
  assert.ok(reopened.filter(e => e === '40:200').length === 2, `layer 40 rescanned once after reopening: ${reopened}`)

  // The backend gives up on a layer (scan budget): the view says so, and the next layer loads.
  await clearLog()
  await setScan({ limit: { 50: true } })
  await set({ layer: 50 })
  await waitLog('50:413')
  await page.waitForFunction(() =>
    document.querySelector('[data-layer-status]')?.textContent.includes('Layer path skipped: file too large to scan in time')
  )
  assert.equal(await unavailable(), 0, 'a scan limit is reported as such, not as an unavailable path')
  await set({ layer: 51 })
  await waitLayer(51)

  assert.deepEqual(errors, [])
  console.log(
    'PASS: layer path flow with a one-scan backend: waiting for layer 1 while preparing, cold cache, layer 1 -> 2, current before previous, busy retry, rapid stepping, back to Live, nozzle updates without fetch, obsolete responses, close/reopen, scan limit reported'
  )
} finally {
  await browser.close()
  await demo.cleanup()
}
