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
  // The status line always names the layer and its total; loading shows as a small mark, never as the text.
  await page.evaluate(() => {
    window.__statusLoadingText = 0
    new MutationObserver(() => {
      if (/Loading layer/.test(document.querySelector('[data-layer-status]')?.textContent || '')) window.__statusLoadingText++
    }).observe(document.body, { subtree: true, childList: true, characterData: true })
  })
  await page.evaluate(() => {
    const state = window.__CC2_PREVIEW
    Object.assign(state, { status: 2, filename: 'flow.gcode', current: '', excluded: [], layer: 0, totalLayers: 227, nozzleX: 60 })
    state.objectRows = [{ name: 'Part', center: [128, 128] }]
    const scan = (state.scan = { busyUntil: 0, duration: 300, durations: {}, fail: {}, limit: {}, held: false, heldAt: [], log: [] })
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
          d.print.duration = state.duration ?? 600
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
        if (scan.held) {
          // The read hold: the printer homes, levels, probes or starts a print, and the service reads no file.
          scan.log.push(`${layer}:held`)
          scan.heldAt.push(now)
          return Promise.resolve(json({ error: 'Printer is homing or starting a print; retry shortly' }, 503))
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
  // The bed map is on the Job page itself: one card with the Objects and Layer path tabs.
  const dialog = page.locator('section:has(.cc2-job-bar)')
  const unavailable = () => dialog.getByText('Layer path not available', { exact: true }).count()
  const openLayerTab = async () => {
    const tab = dialog.getByRole('button', { name: 'Layer path', exact: true })
    await tab.waitFor()
    await page.waitForFunction(() => !Array.from(document.querySelectorAll('fieldset button')).find(b => b.textContent.trim() === 'Layer path')?.disabled)
    await tab.click()
  }
  // Leaving the Layer path tab drops the loaded path and aborts what is pending, like closing the old dialog did.
  const closeLayerTab = async () => {
    await dialog.getByRole('button', { name: 'Objects', exact: true }).click()
    await page.waitForFunction(() => document.querySelector('[data-bed-map-tab]')?.getAttribute('data-bed-map-tab') === 'objects')
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
  await page.waitForFunction(() => /Layer 10 shown · printer at 20, loading…/.test(document.querySelector('[data-layer-status]')?.textContent || ''))
  assert.equal(await dialog.getByText('Loading layer…', { exact: true }).count(), 0, 'a path already on screen is not covered by loading')
  assert.ok(await dialog.locator('[data-layer-path="printed"], [data-layer-path="remain"]').count(), 'layer 10 stays visible while 20 is busy')
  assert.equal(await unavailable(), 0, 'busy is not shown as unavailable')
  await waitLayer(20)
  const busy = await log()
  assert.ok(busy.filter(e => e === '20:503').length >= 1 && busy.includes('20:200'), `busy current layer retried: ${busy}`)
  assert.ok(busy.every(e => e.startsWith('20:') || e.startsWith('19:')), `only the latest layer is retried: ${busy}`)
  await page.waitForTimeout(800)

  // Read hold (the printer homes or starts a print): the layer waits with its own text, asks again only every 3 s,
  // never gives up and never says "not available"; the path on screen stays. When the hold ends, the layer comes.
  await clearLog()
  await setScan({ held: true, heldAt: [] })
  await set({ layer: 21 })
  await page.waitForFunction(() => window.__CC2_PREVIEW.scan.heldAt.length >= 3, null, { timeout: 12000 })
  await page.waitForFunction(() => /Waiting until the printer is ready/.test(document.querySelector('[data-layer-status]')?.textContent || ''))
  const heldAt = await page.evaluate(() => [...window.__CC2_PREVIEW.scan.heldAt])
  const heldGaps = heldAt.slice(1).map((t, i) => t - heldAt[i])
  assert.ok(heldGaps.every(gap => gap >= 2950), `the read hold is asked again every 3 s, not faster: ${heldGaps}`)
  assert.equal(await unavailable(), 0, 'the read hold is not shown as unavailable')
  assert.ok(await dialog.locator('[data-layer-path="printed"], [data-layer-path="remain"]').count(), 'layer 20 stays visible while 21 waits')
  assert.ok((await log()).every(e => e === '21:held'), `only the printer's layer is asked for: ${await log()}`)
  await setScan({ held: false })
  await waitLayer(21)
  assert.doesNotMatch(await dialog.locator('[data-layer-status]').innerText(), /Waiting until the printer is ready/)
  await set({ layer: 20 })
  await waitLayer(20)
  await page.waitForTimeout(600)

  // Rapid manual stepping requests only the layer it settles on.
  await clearLog()
  const next = dialog.getByRole('button', { name: 'Next layer' })
  for (let i = 0; i < 5; i++) await next.click()
  await waitLayer(25)
  await waitLog('24:200')
  await page.waitForTimeout(600)
  assert.deepEqual(await log(), ['25:200', '24:200'], 'rapid stepping 20 -> 25 scans 25 and then its previous layer only')

  // Browsing by hand: the nozzle is not drawn (it would move over the layer being studied); Live draws it again.
  assert.equal(await dialog.locator('[data-layer-nozzle]').count(), 0, 'no nozzle while browsing by hand')
  assert.equal(await dialog.locator('.cc2-layer-nozzle-swatch').count(), 0, 'its legend entry is hidden as well')

  // Back to Live: layer 20 is cached, so nothing is scanned.
  await clearLog()
  await dialog.getByRole('button', { name: 'Live', exact: true }).click()
  await waitLayer(20)
  assert.equal(await dialog.locator('[data-layer-nozzle]').count(), 1, 'Live draws the nozzle')
  assert.equal(await dialog.locator('.cc2-layer-nozzle-swatch').count(), 1, 'and its legend entry')
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

  // Leaving and reopening the Layer path tab while a request is pending.
  await clearLog()
  await setScan({ durations: { 40: 1500 } })
  await dialog.getByRole('button', { name: 'Live', exact: true }).click()
  await set({ layer: 40 })
  await waitLog('40:200')
  await closeLayerTab()
  await openLayerTab()
  await waitLayer(40)
  assert.equal(await unavailable(), 0, 'reopening during a pending scan waits for it instead of failing')
  const reopened = await log()
  assert.ok(reopened.filter(e => e === '40:200').length === 2, `layer 40 rescanned once after reopening: ${reopened}`)

  // The printer changes layer faster than the reply. The in-flight path is drawn
  // and named as behind; the view does not stay on "Loading layer…". Then the
  // newest layer is requested, skipping the ones the printer already left.
  await clearLog()
  await setScan({ durations: { 60: 1200, 63: 800 } })
  await set({ layer: 60 })
  await waitLog('60:200')
  await set({ layer: 63 })
  await page.waitForFunction(() => {
    const status = document.querySelector('[data-layer-status]')?.textContent || ''
    const drawn = Array.from(document.querySelectorAll('[data-layer-path="printed"], [data-layer-path="remain"]')).some(el =>
      (el.getAttribute('points') || '').split(' ').includes(`200,${256 - (20 + (60 % 50))}`)
    )
    return drawn && status.includes('Layer 60 shown · printer at 63, loading…')
  })
  assert.equal(await dialog.getByText('Loading layer…', { exact: true }).count(), 0, 'loading does not cover the path that already arrived')
  assert.equal(await dialog.locator('[data-layer-behind-hint]').count(), 1, 'the hint about the kept path shows while the path is behind')
  await waitLayer(63)
  assert.equal(await dialog.locator('[data-layer-behind-hint]').count(), 0, 'and goes once the path has caught up')

  const jumped = await log()
  assert.ok(jumped.includes('60:200') && jumped.includes('63:200'), `finished the slow layer, then the newest: ${jumped}`)
  assert.ok(!jumped.some(e => e.startsWith('61:')), `does not scan layers the printer already left: ${jumped}`)
  const at63 = jumped.indexOf('63:200')
  const at62 = jumped.indexOf('62:200')
  assert.ok(at62 === -1 || at62 > at63, `layer 62 is only the previous layer, after 63: ${jumped}`)

  // One layer behind for a moment is the normal load of the next layer: the status
  // names the drawn layer with the total and does not flash the lag or its hint.
  await clearLog()
  await setScan({ durations: { 64: 900 } })
  await set({ layer: 64 })
  await page.waitForTimeout(300)
  const brief = await dialog.locator('[data-layer-status]').innerText()
  assert.match(brief, /^Layer 63 \/ 227/, `a short one-layer lag keeps the plain status: ${brief}`)
  assert.equal(await dialog.getByText('printer at', { exact: false }).count(), 0, 'a short one-layer lag is not named')
  assert.equal(await dialog.locator('[data-layer-behind-hint]').count(), 0, 'and shows no hint')
  await waitLayer(64)

  // No path yet, and the scan outlasts the stated 20 s budget: say so, then show the path when it arrives.
  // Close first so the next open starts with an empty path.
  await closeLayerTab()
  await clearLog()
  const polls = await page.evaluate(() => window.__CC2_PREVIEW.printerPolls || 0)
  await setScan({ durations: { 70: 22000 } })
  await set({ layer: 70 })
  await page.waitForFunction(n => window.__CC2_PREVIEW.printerPolls > n, polls)
  await openLayerTab()
  await waitLog('70:200')
  await page.waitForFunction(
    () => document.querySelector('[data-layer-status]')?.textContent.includes('Layer path did not answer in time'),
    { timeout: 25000 }
  )
  assert.equal(await dialog.getByText('Loading layer…', { exact: true }).count(), 0, 'the loading text ends at the timeout')
  await page.waitForFunction(
    n =>
      new RegExp(`Layer ${n} / 227`).test(document.querySelector('[data-layer-status]')?.textContent || '') &&
      Array.from(document.querySelectorAll('[data-layer-path="printed"], [data-layer-path="remain"]')).some(el =>
        (el.getAttribute('points') || '').split(' ').includes(`200,${256 - (20 + (n % 50))}`)
      ),
    70,
    { timeout: 10000 }
  )

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

  // A new print that still reports the previous job's layer (seen: layer 193 right after a start):
  // Live does not scan it. Young prints only follow layers that climb from the start; a jump waits
  // until the print is older than the start grace.
  await clearLog()
  // First a layer above the new job's layer count (1000 from a long previous print, the new one has 193).
  await set({ filename: 'restart.gcode', totalLayers: 193, layer: 1000, duration: 600 })
  // A new job keeps the tab open and follows the new file.
  await openLayerTab()
  await page.waitForTimeout(2500)
  assert.ok(!(await log()).some(e => e.startsWith('1000:')), `a layer above the layer count is not scanned: ${await log()}`)
  await set({ layer: 193, duration: 5 })
  await page.waitForTimeout(2500)
  assert.ok(!(await log()).some(e => e.startsWith('193:')), `stale layer not scanned: ${await log()}`)
  await page.waitForFunction(() => document.querySelector('[data-layer-status]')?.textContent.includes('Waiting for layer 1'))
  await set({ layer: 1, duration: 8 })
  await waitLog('1:200')
  await set({ layer: 2, duration: 12 })
  await waitLog('2:200')
  await set({ layer: 150, duration: 15 })
  await page.waitForTimeout(2500)
  assert.ok(!(await log()).some(e => e.startsWith('150:')), `a jump in a young print is not followed: ${await log()}`)
  // A stale layer can stand for minutes (heating, homing, probing): still not followed after 4 minutes.
  await set({ duration: 240 })
  await page.waitForTimeout(2000)
  assert.ok(!(await log()).some(e => e.startsWith('150:')), `a standing jump is not followed while the print is young: ${await log()}`)
  // A value that steps up by one is live, not stale: followed at once.
  await set({ layer: 151, duration: 250 })
  await waitLog('151:200')
  // An old print follows any layer.
  await set({ layer: 170, duration: 400 })
  await waitLog('170:200')
  await set({ filename: 'flow.gcode', totalLayers: 227, layer: 51, duration: 600 })
  await openLayerTab()
  await waitLayer(51)

  // Phone width: the longer catch-up text wraps on its own line instead of being clipped,
  // and the map does not move when it appears.
  await page.setViewportSize({ width: 390, height: 844 })
  await set({ layer: 125 })
  await waitLayer(125)
  const phone = () =>
    page.evaluate(() => {
      const status = document.querySelector('[data-layer-status]')
      return {
        text: status.textContent,
        fits: status.scrollWidth <= status.clientWidth && status.getBoundingClientRect().right <= document.documentElement.clientWidth,
        mapTop: Math.round(document.querySelector('.cc2-object-map-frame').getBoundingClientRect().top),
      }
    })
  const steady = await phone()
  await setScan({ durations: { 127: 6000 } })
  await set({ layer: 127 })
  await page.waitForFunction(() => /Layer 125 shown · printer at 127, loading…/.test(document.querySelector('[data-layer-status]')?.textContent || ''))
  const behind = await phone()
  assert.ok(steady.fits && behind.fits, `status fits at phone width: ${JSON.stringify([steady, behind])}`)
  assert.equal(behind.mapTop, steady.mapTop, 'the map does not move when the catch-up text appears')
  await page.setViewportSize({ width: 1440, height: 1000 })

  assert.equal(await page.evaluate(() => window.__statusLoadingText), 0, 'the status line never turns into "Loading layer…"')
  assert.deepEqual(errors, [])
  console.log(
    'PASS: layer path flow with a one-scan backend: waiting for layer 1 while preparing, cold cache, layer 1 -> 2, current before previous, busy retry, read hold every 3 s with its own text, rapid stepping, back to Live, nozzle updates without fetch, obsolete responses, close/reopen, scan limit reported, stale layer after a print start, catch-up text at phone width'
  )
} finally {
  await browser.close()
  await demo.cleanup()
}
