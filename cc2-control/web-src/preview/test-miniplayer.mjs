import assert from 'node:assert/strict'
import { createServer as httpServer } from 'node:http'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

// The mini player: one floating camera view that stays open across pages, can be moved and resized (16:9,
// 240-640 px, at most half the window), zooms digitally up to 2x, and closes by itself when another viewer
// takes the camera (also when it was paused in a hidden tab). It must never open a second stream, and moving or zooming
// it must not draw the whole app again. The camera service is a local MJPEG server.
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
const jpeg = Buffer.from('/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAQABADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDm6KKK+xPhD//Z', 'base64')
let clients = 0, requests = 0, reject = false
let peak = 0, overlapSince = 0, overlapMs = 0 // most streams open at once, and for how long there were two or more
const resetPeak = () => { peak = clients; overlapMs = 0; overlapSince = clients > 1 ? Date.now() : 0 }
const stream = httpServer((req, res) => {
  requests++
  if (reject) { res.writeHead(503); res.end(); return }
  clients++
  peak = Math.max(peak, clients)
  if (clients === 2) overlapSince = Date.now()
  res.writeHead(200, { 'Content-Type': 'multipart/x-mixed-replace; boundary=frame', 'Cache-Control': 'no-store' })
  const frame = () => { res.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`); res.write(jpeg); res.write('\r\n') }
  frame()
  const timer = setInterval(frame, 100)
  res.on('close', () => { clearInterval(timer); if (clients === 2 && overlapSince) overlapMs += Date.now() - overlapSince; clients-- })
})
let browser
const wait = async (predicate, label) => {
  for (let i = 0; i < 100; i++) { if (await predicate()) return; await new Promise(r => setTimeout(r, 50)) }
  throw new Error(label)
}
// A fixed pause is only for "nothing may happen" checks (a reconnect, a stolen camera): there is nothing to wait for.
const observe = ms => new Promise(r => setTimeout(r, ms))
const near = (actual, expected, tolerance, label) =>
  assert.ok(Math.abs(actual - expected) <= tolerance, `${label}: ${actual} is not within ${tolerance} of ${expected}`)
try {
  await new Promise(r => stream.listen(0, '127.0.0.1', r))
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : {}) })
  const errors = []
  const printerReadTimes = []
  const preferences = { language: 'en', theme: 'dark', camera_persistent: false }
  const newContext = async (viewport, extra = {}) => {
    const context = await browser.newContext({ viewport, ...extra })
    await context.route('**/api/printer', async r => { printerReadTimes.push(Date.now()); await r.continue() })
    await context.route('**/api/preferences', async r => {
      if (r.request().method() === 'PUT') Object.assign(preferences, r.request().postDataJSON())
      await r.fulfill({ json: r.request().method() === 'PUT' ? { saved: true } : preferences })
    })
    await context.route('**/__preview/camera.svg*', r => r.fulfill({ status: 302, headers: { location: `http://127.0.0.1:${stream.address().port}/stream` } }))
    return context
  }
  const context = await newContext({ width: 1440, height: 1000 })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(`${origin}/#dashboard`)
  const region = page.getByRole('region', { name: 'Mini player live camera', exact: true })
  const link = name => page.locator('.cc2-sidebar').getByRole('link', { name, exact: true })
  const box = async () => region.boundingBox()
  const stored = () => page.evaluate(() => JSON.parse(localStorage.getItem('cc2-miniplayer') || 'null'))
  const zoomOf = () => page.evaluate(() => {
    const img = document.querySelector('.cc2-miniplayer img')
    if (!img) return null
    const m = new DOMMatrix(getComputedStyle(img).transform)
    return { scale: m.a, x: m.e, y: m.f }
  })

  // The size of the area around a control's centre that really reaches it (what a finger can hit), by hit testing.
  const hitArea = locator => locator.evaluate(el => {
    const r = el.getBoundingClientRect()
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2
    const reach = (dx, dy) => {
      let n = 0
      while (n < 40) {
        const hit = document.elementFromPoint(cx + dx * (n + 1), cy + dy * (n + 1))
        if (!hit || !(hit === el || el.contains(hit))) break
        n++
      }
      return n + 1
    }
    return { w: reach(-1, 0) + reach(1, 0), h: reach(0, -1) + reach(0, 1) }
  })
  // Counts how often the app and the camera card are drawn, by hooking Preact's render callback (same module instance).
  // A reload starts the counting over, so it is installed right before it is needed.
  const countDraws = () => page.evaluate(async () => {
    const url = performance.getEntriesByType('resource').map(e => e.name).find(n => /\/deps\/preact\.js/.test(n))
    const { options } = await import(url)
    window.__draws = { app: 0, card: 0, mini: 0 }
    const before = options.diffed
    options.diffed = vnode => {
      const name = typeof vnode.type === 'function' ? vnode.type.name : ''
      if (name === 'App') window.__draws.app++
      else if (name === 'MiniPlayer') window.__draws.mini++
      else if (name === 'CameraStream' && !vnode.props.layout) window.__draws.card++
      before?.(vnode)
    }
  })
  const draws = () => page.evaluate(() => ({ ...window.__draws }))

  // 0. Layout and hand-over checks. Each one runs alone in its own page and reports "ok" or "RED" at once, so a missing
  // change shows up as its own line (the numbered steps below stop at their first failure).
  const softFailures = []
  const soft = async (name, fn) => {
    try { await fn(); console.log(`  ok   ${name}`) } catch (e) { softFailures.push(name); console.log(`  RED  ${name}: ${String(e.message).split('\n')[0].slice(0, 200)}`) }
  }
  const lc = await newContext({ width: 1440, height: 1000 })
  const fresh = async (setup, ctx = lc) => {
    const p = await ctx.newPage()
    p.on('pageerror', e => errors.push(e.message))
    if (setup) await p.addInitScript(setup)
    await p.goto(`${origin}/#dashboard`)
    return p
  }
  const regionOf = p => p.getByRole('region', { name: 'Mini player live camera', exact: true })
  const openMini = async p => {
    await p.getByRole('button', { name: 'Mini player', exact: true }).first().click()
    await regionOf(p).waitFor()
    await regionOf(p).locator('img').waitFor({ state: 'visible' })
  }
  const rect = async locator => locator.boundingBox()
  const inside = (inner, outer, tolerance = 1) =>
    inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.width <= outer.x + outer.width + tolerance && inner.y + inner.height <= outer.y + outer.height + tolerance
  const dragFrom = async (p, from, by, steps = 5) => {
    await p.mouse.move(from[0], from[1])
    await p.mouse.down()
    await p.mouse.move(from[0] + by[0], from[1] + by[1], { steps })
    await p.mouse.up()
  }
  const keepClosed = async p => { await p.close(); await wait(() => clients === 0, 'a closed page ends its stream') }

  await soft('L1 the bar is on top, the size handle sits in the top left corner, every control is in the bar', async () => {
    const p = await fresh()
    await openMini(p)
    const r = regionOf(p)
    const win = await rect(r), bar = await rect(r.locator('.cc2-mini-bar')), frame = await rect(r.locator('.cc2-camera-frame'))
    assert.ok(bar.y < frame.y, `bar (${bar.y}) must lie above the picture (${frame.y})`)
    near(bar.y, win.y + 1, 2, 'the bar starts at the top of the window')
    const handle = await rect(r.getByRole('slider', { name: 'Mini player size', exact: true }))
    near(handle.x, win.x + 1, 2, 'size handle at the left edge'); near(handle.y, win.y + 1, 2, 'size handle at the top edge')
    near(handle.width, 44, 1, 'size handle 44 px'); near(handle.height, 44, 1, 'size handle 44 px')
    for (const [name, target] of [['Close', r.getByRole('button', { name: 'Close mini player', exact: true })], ['Pause', r.getByRole('button', { name: 'Pause', exact: true })], ['Zoom in', r.getByRole('button', { name: /^Zoom in/ })], ['Zoom out', r.getByRole('button', { name: /^Zoom out/ })], ['Grip', r.getByRole('button', { name: 'Move mini player', exact: true })]]) {
      assert.ok(inside(await rect(target), bar), `${name} must be inside the bar`)
    }
    await keepClosed(p)
  })

  await soft('L2 resizing anchors the bottom right corner (mouse and keys)', async () => {
    const p = await fresh()
    await openMini(p)
    const r = regionOf(p)
    const slider = r.getByRole('slider', { name: 'Mini player size', exact: true })
    const start = await rect(r)
    const h = await rect(slider)
    await dragFrom(p, [h.x + 6, h.y + 6], [-120, -68])
    const grown = await rect(r)
    assert.ok(grown.width > start.width + 80, `dragging towards the top left grows it (${start.width} -> ${grown.width})`)
    near(grown.x + grown.width, start.x + start.width, 2, 'right edge stays'); near(grown.y + grown.height, start.y + start.height, 2, 'bottom edge stays')
    const h2 = await rect(slider)
    await dragFrom(p, [h2.x + 6, h2.y + 6], [90, 51])
    const shrunk = await rect(r)
    assert.ok(shrunk.width < grown.width - 60, 'dragging towards the bottom right shrinks it')
    near(shrunk.x + shrunk.width, start.x + start.width, 2, 'right edge stays when shrinking')
    await slider.focus()
    await p.keyboard.press('End')
    const max = await rect(r)
    near(max.width, 640, 1, 'End: maximum'); near(max.x + max.width, start.x + start.width, 2, 'End keeps the right edge'); near(max.y + max.height, start.y + start.height, 2, 'End keeps the bottom edge')
    await keepClosed(p)
  })

  await soft('L3 the whole bar and the frame strips move the window; buttons do not', async () => {
    const p = await fresh()
    await openMini(p)
    const r = regionOf(p)
    let before = await rect(r)
    const name = await rect(r.locator('.cc2-mini-name'))
    await dragFrom(p, [name.x + name.width / 2, name.y + name.height / 2], [-200, -100])
    let after = await rect(r)
    near(after.x, before.x - 200, 3, 'drag on the bar moves left'); near(after.y, before.y - 100, 3, 'drag on the bar moves up')
    before = after
    const pause = await rect(r.getByRole('button', { name: 'Pause', exact: true }))
    await dragFrom(p, [pause.x + pause.width / 2, pause.y + pause.height / 2], [-120, -60])
    after = await rect(r)
    near(after.x, before.x, 1, 'a drag that starts on Pause does not move the window'); near(after.y, before.y, 1, 'a drag that starts on Pause does not move the window')
    await wait(() => clients === 0 || clients === 1, 'settled')
    for (const edge of ['l', 'r', 'b']) {
      before = await rect(r)
      const strip = await rect(r.locator(`.cc2-mini-edge-${edge}`))
      await dragFrom(p, [strip.x + strip.width / 2, strip.y + strip.height / 2], [-40, 30])
      after = await rect(r)
      near(after.x, before.x - 40, 3, `drag on the ${edge} frame strip moves left`); near(after.y, before.y + 30, 3, `drag on the ${edge} frame strip moves down`)
    }
    await keepClosed(p)
  })

  await soft('L4 zoomed: dragging in the picture pans the picture, dragging the bar moves the window', async () => {
    const p = await fresh()
    await openMini(p)
    const r = regionOf(p)
    const scale = () => p.evaluate(() => { const m = new DOMMatrix(getComputedStyle(document.querySelector('.cc2-miniplayer img')).transform); return { s: m.a, x: m.e, y: m.f } })
    await r.getByRole('button', { name: /^Zoom in/ }).click(); await r.getByRole('button', { name: /^Zoom in/ }).click()
    const win = await rect(r), frame = await rect(r.locator('.cc2-camera-frame'))
    await dragFrom(p, [frame.x + frame.width / 2, frame.y + frame.height / 2], [30, 20])
    near((await scale()).x, 30, 2, 'the picture panned'); near((await rect(r)).x, win.x, 1, 'the window stayed')
    const panned = await scale()
    const name = await rect(r.locator('.cc2-mini-name'))
    await dragFrom(p, [name.x + name.width / 2, name.y + name.height / 2], [-90, -40])
    near((await rect(r)).x, win.x - 90, 3, 'the bar moved the window'); near((await scale()).x, panned.x, 0.5, 'the picture did not pan')
    await keepClosed(p)
  })

  await soft('L5 the bar fits at every width; the close button is in the bar from 198 px, on the picture below', async () => {
    const check = async (p, widths, keys) => {
      const r = regionOf(p)
      const slider = r.getByRole('slider', { name: 'Mini player size', exact: true })
      await slider.focus(); await p.keyboard.press('Home')
      let width = Number(await slider.getAttribute('aria-valuenow'))
      for (const target of widths) {
        while (width < target) { await p.keyboard.press('ArrowRight'); width = Number(await slider.getAttribute('aria-valuenow')) }
        const bad = await p.evaluate(() => {
          const bar = document.querySelector('.cc2-mini-bar'), br = bar.getBoundingClientRect()
          const win = document.querySelector('.cc2-miniplayer').getBoundingClientRect()
          const items = [...bar.querySelectorAll('button, [role="slider"]')].filter(e => e.getClientRects().length && getComputedStyle(e).visibility !== 'hidden')
          const out = []
          const rects = items.map(e => [e.getAttribute('aria-label') || e.className, e.getBoundingClientRect()])
          const closeBar = document.querySelector('.cc2-mini-close').getBoundingClientRect()
          for (const [n, q] of rects) {
            if (n === 'Close mini player') continue
            if (q.left < br.left - 0.5 || q.right > br.right + 0.5 || q.top < br.top - 0.5 || q.bottom > br.bottom + 0.5) out.push(`${n} outside the bar`)
          }
          for (let i = 0; i < rects.length; i++) for (let j = i + 1; j < rects.length; j++) {
            const a = rects[i][1], b = rects[j][1]
            if (a.left < b.right - 0.5 && b.left < a.right - 0.5 && a.top < b.bottom - 0.5 && b.top < a.bottom - 0.5) out.push(`${rects[i][0]} overlaps ${rects[j][0]}`)
          }
          if (bar.scrollWidth > bar.clientWidth + 0.5) out.push('the bar overflows')
          const closeIn = closeBar.top >= br.top - 0.5 && closeBar.bottom <= br.bottom + 0.5
          return { out, closeIn, winWidth: win.width, closeInside: closeBar.right <= win.right + 0.5 && closeBar.left >= win.left - 0.5 }
        })
        assert.deepEqual(bad.out, [], `width ${width}`)
        assert.equal(bad.closeIn, width >= 198, `width ${width}: the close button ${bad.closeIn ? 'is' : 'is not'} in the bar`)
        assert.equal(bad.closeInside, true, `width ${width}: the close button stays inside the window`)
      }
    }
    const p = await fresh()
    await openMini(p)
    await check(p, [240, 256, 272, 288, 304, 336, 352, 640])
    await keepClosed(p)
    const phoneCtx = await newContext({ width: 393, height: 780 }, { hasTouch: true, isMobile: true, deviceScaleFactor: 2 })
    const pp = await fresh(null, phoneCtx)
    await openMini(pp)
    await check(pp, [160, 176, 192, 208, 224, 240, 256])
    await pp.close(); await phoneCtx.close()
    await wait(() => clients === 0, 'closed')
  })

  await soft('L6 the keyboard order follows the picture: size, grip, pause, zoom, close, image', async () => {
    const p = await fresh()
    await openMini(p)
    const r = regionOf(p)
    await r.getByRole('slider', { name: 'Mini player size', exact: true }).focus()
    const seen = []
    for (let i = 0; i < 6; i++) {
      seen.push(await p.evaluate(() => document.activeElement?.getAttribute('aria-label') || ''))
      await p.keyboard.press('Tab')
    }
    const labels = seen.map(x => (/^Zoom out/.test(x) ? 'Zoom out' : /^Zoom in/.test(x) ? 'Zoom in' : /^Image section/.test(x) ? 'Image' : x))
    assert.deepEqual(labels, ['Mini player size', 'Move mini player', 'Pause', 'Zoom out', 'Zoom in', 'Close mini player'])
    assert.match(await p.evaluate(() => document.activeElement?.getAttribute('aria-label') || ''), /^Image section/, 'the picture comes after the bar')
    await keepClosed(p)
  })

  await soft('L7 the job page offers Mini player while its camera is on, and hands the picture over', async () => {
    const p = await fresh()
    await p.goto(`${origin}/#job`)
    const cam = p.getByRole('button', { name: 'Camera', exact: true })
    await cam.waitFor()
    assert.equal(await p.locator('.cc2-job-camera').count(), 0, 'no picture, no button before the camera is on')
    await cam.click()
    await p.locator('.cc2-job-camera img').waitFor({ state: 'visible' })
    await wait(() => clients === 1, 'job camera streams')
    const hand = p.locator('.cc2-job-camera').getByRole('button', { name: 'Mini player', exact: true })
    await hand.waitFor({ state: 'visible', timeout: 3000 })
    const hb = await rect(hand)
    assert.ok(hb.width >= 43 && hb.height >= 43, `button at least 44 x 44 (${hb.width} x ${hb.height})`)
    resetPeak()
    await hand.click()
    await regionOf(p).waitFor()
    await wait(() => clients === 1, 'one stream after the hand-over')
    assert.equal(await p.locator('.cc2-job-camera').count(), 0, 'the page picture is gone')
    assert.equal(await cam.getAttribute('aria-pressed'), 'false', 'the Camera button is off')
    await observe(500)
    assert.equal(peak, 1, 'never two streams at once'); assert.equal(overlapMs, 0, 'no overlap')
    await keepClosed(p)
  })

  await soft('L8 entering the job page with a remembered camera does not take the camera from an open mini player', async () => {
    const p = await fresh()
    await p.evaluate(() => localStorage.setItem('cc2-job-camera', '1'))
    await openMini(p)
    await wait(() => clients === 1, 'mini streams')
    resetPeak()
    const before = requests
    await p.locator('.cc2-sidebar').getByRole('link', { name: 'Job', exact: true }).click()
    await p.waitForFunction(() => location.hash === '#job')
    await observe(2600)
    assert.equal(await regionOf(p).count(), 1, 'the mini player stays open')
    assert.equal(clients, 1, 'one stream'); assert.equal(requests, before, 'the page requested nothing'); assert.equal(peak, 1)
    assert.equal(await p.locator('.cc2-job-camera').count(), 0, 'the job page did not start its picture')
    assert.equal(await p.getByRole('button', { name: 'Camera', exact: true }).getAttribute('aria-pressed'), 'false')
    await p.getByRole('button', { name: 'Camera', exact: true }).click() // a deliberate action: the page takes over
    await wait(() => regionOf(p).count().then(n => n === 0), 'the deliberate Camera button closes the mini player')
    await p.locator('.cc2-job-camera img').waitFor({ state: 'visible' })
    await wait(() => clients === 1, 'the job page streams')
    await keepClosed(p)
  })

  await soft('L9 same tab: starting a card or a page picture closes the mini player first (no overlap)', async () => {
    const p = await fresh()
    await openMini(p)
    await wait(() => clients === 1, 'mini streams')
    resetPeak()
    await p.getByRole('button', { name: 'Start live view', exact: true }).click()
    await p.locator('.cc2-camera-card img').waitFor({ state: 'visible' })
    await wait(() => clients === 1, 'the card streams')
    await wait(() => regionOf(p).count().then(n => n === 0), 'the mini player closed')
    await observe(600)
    assert.equal(peak, 1, `never two streams at once (peak ${peak})`); assert.equal(overlapMs, 0, `no overlap (${overlapMs} ms)`)
    await keepClosed(p)
  })

  await soft('L10 same browser, other tab: the old viewer ends before the new one starts', async () => {
    const a = await fresh()
    await openMini(a)
    await wait(() => clients === 1, 'mini streams')
    const b = await fresh()
    resetPeak()
    await b.getByRole('button', { name: 'Start live view', exact: true }).click()
    await b.locator('.cc2-camera-card img').waitFor({ state: 'visible' })
    await wait(() => regionOf(a).count().then(n => n === 0), 'the mini player in the other tab closed')
    await wait(() => clients === 1, 'one stream')
    await observe(600)
    assert.equal(peak, 1, `never two streams at once (peak ${peak})`); assert.equal(overlapMs, 0, `no overlap (${overlapMs} ms)`)
    await a.close(); await keepClosed(b)
  })

  await soft('L11 without BroadcastChannel the hand-over still works in the same tab and nothing throws', async () => {
    const p = await fresh(() => { Object.defineProperty(window, 'BroadcastChannel', { value: undefined, configurable: true }) })
    await openMini(p)
    await wait(() => clients === 1, 'mini streams')
    resetPeak()
    await p.getByRole('button', { name: 'Start live view', exact: true }).click()
    await p.locator('.cc2-camera-card img').waitFor({ state: 'visible' })
    await wait(() => regionOf(p).count().then(n => n === 0), 'the mini player closed')
    await observe(400)
    assert.equal(peak, 1, `never two streams at once (peak ${peak})`)
    await keepClosed(p)
  })

  await soft('L12 the Mini player button is a toggle: second press closes it and ends the stream', async () => {
    const p = await fresh()
    const button = p.getByRole('button', { name: 'Mini player', exact: true })
    assert.equal(await button.getAttribute('aria-pressed'), 'false')
    await button.click()
    await regionOf(p).waitFor()
    await wait(() => clients === 1, 'opened')
    assert.equal(await button.getAttribute('aria-pressed'), 'true')
    await button.click()
    await wait(() => regionOf(p).count().then(n => n === 0), 'the second press closes the window')
    await wait(() => clients === 0, 'no stream any more')
    assert.equal(await button.getAttribute('aria-pressed'), 'false')
    assert.equal(await p.evaluate(() => document.activeElement?.hasAttribute('data-miniplayer-open')), true, 'the focus stays on the button')
    await p.getByRole('button', { name: 'Start live view', exact: true }).waitFor({ state: 'visible' })
    await observe(2200)
    assert.equal(clients, 0, 'nothing starts by itself afterwards')
    // The card was streaming: open, close again: the card does not start again.
    await p.getByRole('button', { name: 'Start live view', exact: true }).click()
    await p.locator('.cc2-camera-card img').waitFor({ state: 'visible' })
    await button.click(); await regionOf(p).waitFor(); await wait(() => clients === 1, 'mini has the picture')
    await button.click()
    await wait(() => clients === 0, 'closed again, the card does not take the picture back')
    await p.getByRole('button', { name: 'Start live view', exact: true }).waitFor({ state: 'visible' })
    await keepClosed(p)
  })

  await soft('L13 another device (no shared storage): the overlap is bounded by one poll interval (documents the limit, also green before the change)', async () => {
    const other = await newContext({ width: 1440, height: 1000 })
    const a = await fresh()
    await openMini(a)
    await wait(() => clients === 1, 'mini streams')
    const b = await fresh(null, other)
    resetPeak()
    await b.getByRole('button', { name: 'Start live view', exact: true }).click()
    await b.locator('.cc2-camera-card img').waitFor({ state: 'visible' })
    await wait(() => regionOf(a).count().then(n => n === 0), 'the mini player closed after the next poll')
    await wait(() => clients === 1, 'one stream')
    console.log(`       measured: overlap ${overlapMs} ms, at most ${peak} streams (other device)`)
    assert.ok(overlapMs <= 3000, `overlap ${overlapMs} ms stays below 3 s`)
    await a.close(); await keepClosed(b); await other.close()
  })
  await lc.close()
  requests = 0 // the numbered steps below count requests from here

  // 1. The dashboard offers the mini player next to the two existing buttons; nothing streams yet.
  const open = page.getByRole('button', { name: 'Mini player', exact: true })
  await open.waitFor()
  await page.getByRole('button', { name: 'Open in new window', exact: true }).waitFor()
  await page.getByRole('button', { name: 'Snapshot', exact: true }).waitFor()
  assert.equal(await region.count(), 0, 'no mini player before it is opened')
  assert.equal(requests, 0, 'nothing may request the camera before the mini player is opened')

  // 2. Opening it starts exactly one stream, bottom right, 360 px wide, 16:9 plus a 44 px bar.
  await open.click()
  await region.waitFor()
  await wait(() => clients === 1, 'the mini player opens one stream')
  await region.locator('img').waitFor({ state: 'visible' })
  let b = await box()
  near(b.width, 360, 1, 'default width')
  near(b.height, Math.round(360 * 9 / 16) + 44, 2, 'default height')
  near(b.x + b.width, 1440 - 16, 2, 'default right margin')
  near(b.y + b.height, 1000 - 16, 2, 'default bottom margin')
  await page.getByRole('button', { name: 'Start live view', exact: true }).first().waitFor({ state: 'visible' }) // the dashboard card is idle
  assert.equal(clients, 1, 'the dashboard card did not open a second stream')

  // 3. It survives every page change with the same image element and no reconnect.
  await page.evaluate(() => { window.__miniImage = document.querySelector('.cc2-miniplayer img') })
  const unchanged = requests
  for (const section of ['Control', 'Job', 'Files', 'Settings', 'Dashboard']) {
    await link(section).click()
    await page.waitForFunction(hash => location.hash === hash, '#' + section.toLowerCase())
    await observe(300)
    assert.equal(await region.count(), 1, section + ': the mini player stays open')
    assert.equal(clients, 1, section + ': one stream')
    assert.equal(requests, unchanged, section + ': navigation must not reconnect')
    assert.equal(await page.evaluate(() => document.querySelector('.cc2-miniplayer img') === window.__miniImage), true, section + ': same image element')
  }
  // The job page keeps its own Camera button: it is not greyed out and not hidden by the mini player.
  await link('Job').click()
  const jobCamera = page.getByRole('button', { name: 'Camera', exact: true })
  assert.equal(await jobCamera.isEnabled(), true, 'the Camera button stays usable while the mini player is open')
  await link('Dashboard').click()
  await page.waitForFunction(() => location.hash === '#dashboard')
  await open.waitFor()
  await observe(400) // a route change moves the focus to <main>: let it settle before focusing a control

  // 4. Size: the corner is a slider, 240 px to 640 px (half of 1440 would be 720), always 16:9.
  const size = region.getByRole('slider', { name: 'Mini player size', exact: true })
  assert.equal(await size.getAttribute('aria-valuemin'), '240')
  assert.equal(await size.getAttribute('aria-valuemax'), '640')
  assert.equal(await size.getAttribute('aria-valuenow'), '360')
  await size.focus()
  await page.keyboard.press('ArrowRight')
  b = await box(); near(b.width, 376, 1, 'arrow adds 16 px')
  await page.keyboard.press('Shift+ArrowRight')
  b = await box(); near(b.width, 440, 1, 'shift adds 64 px')
  await page.keyboard.press('Home')
  b = await box(); near(b.width, 240, 1, 'Home is the minimum width')
  near(b.height, Math.round(240 * 9 / 16) + 44, 2, 'minimum height keeps 16:9')
  await page.keyboard.press('ArrowLeft')
  b = await box(); near(b.width, 240, 1, 'never below 240 px')
  await page.keyboard.press('End')
  b = await box(); near(b.width, 640, 1, 'End is the maximum width')
  assert.equal(await size.getAttribute('aria-valuetext'), '640 pixels wide')
  await page.keyboard.press('ArrowRight')
  b = await box(); near(b.width, 640, 1, 'never above 640 px')
  assert.ok(b.x >= 0 && b.x + b.width <= 1440 && b.y >= 0 && b.y + b.height <= 1000, 'the grown window stays on screen')
  // Dragging the corner (top left) with the mouse: the bottom right corner of the window stays where it is.
  await page.keyboard.press('Home')
  const corner = await size.boundingBox()
  const cx = corner.x + 6, cy = corner.y + 6
  const anchor = await box()
  await page.mouse.move(cx, cy)
  await page.mouse.down()
  await page.mouse.move(cx + 60, cy + 60, { steps: 4 })
  await page.mouse.move(cx - 100, cy - 20, { steps: 4 })
  await page.mouse.up()
  const dragged = await box()
  assert.ok(dragged.width > 300 && dragged.width < 400, `the corner drag changes the width (got ${dragged.width})`)
  near(dragged.x + dragged.width, anchor.x + anchor.width, 2, 'the corner drag keeps the right edge')
  near(dragged.y + dragged.height, anchor.y + anchor.height, 2, 'the corner drag keeps the bottom edge')
  near(dragged.height, Math.round(dragged.width * 9 / 16) + 44, 2, 'a dragged size keeps 16:9')
  assert.equal(clients, 1, 'resizing is not a second stream')
  assert.equal(requests, unchanged, 'resizing must not reconnect')

  // 5. Position: the grip moves it, with the mouse and with the arrow keys, and it never leaves the screen.
  const grip = region.getByRole('button', { name: 'Move mini player', exact: true })
  b = await box()
  const gripBox = await grip.boundingBox()
  const gx = gripBox.x + gripBox.width / 2, gy = gripBox.y + gripBox.height / 2
  await page.mouse.move(gx, gy)
  await page.mouse.down()
  await page.mouse.move(gx - 300, gy - 200, { steps: 6 })
  await page.mouse.up()
  let moved = await box()
  near(moved.x, b.x - 300, 3, 'mouse drag moves left')
  near(moved.y, b.y - 200, 3, 'mouse drag moves up')
  await grip.focus()
  await page.keyboard.press('ArrowRight')
  let step = await box(); near(step.x, moved.x + 16, 1, 'arrow moves 16 px')
  await page.keyboard.press('Shift+ArrowDown')
  moved = await box(); near(moved.y, step.y + 64, 1, 'shift moves 64 px')
  for (let i = 0; i < 40; i++) await page.keyboard.press('Shift+ArrowRight')
  for (let i = 0; i < 40; i++) await page.keyboard.press('Shift+ArrowDown')
  b = await box()
  assert.ok(b.x + b.width <= 1440 && b.y + b.height <= 1000, 'it stops at the right and bottom edge')
  for (let i = 0; i < 40; i++) await page.keyboard.press('Shift+ArrowLeft')
  for (let i = 0; i < 40; i++) await page.keyboard.press('Shift+ArrowUp')
  b = await box()
  const sidebar = await page.locator('.cc2-sidebar').boundingBox()
  const topbar = await page.locator('.cc2-topbar').boundingBox()
  assert.ok(b.x >= sidebar.x + sidebar.width, 'it stops right of the side menu, so the grip stays reachable')
  assert.ok(b.y >= topbar.y + topbar.height, 'it stops below the header')
  assert.equal(clients, 1, 'moving is not a second stream')
  assert.equal(requests, unchanged, 'moving must not reconnect')

  // 6. Size and position are remembered in this browser; the open state is not (the camera never starts by itself).
  const saved = await stored()
  assert.ok(saved && typeof saved.w === 'number' && typeof saved.x === 'number' && typeof saved.y === 'number', 'size and position are stored')
  assert.equal('open' in saved, false, 'the open state is not stored')
  const before = await box()
  await page.reload()
  await open.waitFor()
  await observe(500)
  assert.equal(await region.count(), 0, 'a reload does not reopen the mini player')
  assert.equal(clients, 0, 'a reload does not start the camera')
  await open.click()
  await region.waitFor()
  await wait(() => clients === 1, 'reopened')
  const after = await box()
  near(after.x, before.x, 2, 'remembered x'); near(after.y, before.y, 2, 'remembered y'); near(after.width, before.width, 1, 'remembered width')
  // A stored width beyond today's limits is brought back into them.
  await page.evaluate(() => localStorage.setItem('cc2-miniplayer', JSON.stringify({ w: 5000, x: 9, y: 9 })))
  await page.reload()
  await open.click()
  await region.waitFor()
  b = await box()
  near(b.width, 640, 1, 'a stored width is limited'); assert.ok(b.x + b.width <= 1440 && b.y + b.height <= 1000, 'a stored position is brought on screen')
  await page.evaluate(() => localStorage.setItem('cc2-miniplayer', '{broken'))
  await page.reload()
  await open.click()
  await region.waitFor()
  near((await box()).width, 360, 1, 'unreadable storage falls back to the default')
  await wait(() => clients === 1, 'one stream after the reloads')
  // The demo's own "simulation" bar lies along the bottom edge and would catch clicks: move the window up.
  await region.getByRole('button', { name: 'Move mini player', exact: true }).focus()
  for (let i = 0; i < 4; i++) await page.keyboard.press('Shift+ArrowUp')

  // 7. Pause and play keep the window; the claim stays with this page.
  await region.getByRole('button', { name: 'Pause', exact: true }).click()
  await wait(() => clients === 0, 'Pause closes the request')
  assert.equal(await region.count(), 1, 'Pause keeps the window')
  await region.getByRole('button', { name: 'Start live view', exact: true }).click()
  await wait(() => clients === 1, 'play opens one stream again')

  // 8. Zoom: 1x, 1.5x, 2x (maximum); the picture is only magnified, nothing is requested.
  const zoomIn = region.getByRole('button', { name: /^Zoom in/ })
  const zoomOut = region.getByRole('button', { name: /^Zoom out/ })
  const reset = region.getByRole('button', { name: 'Reset zoom', exact: true })
  const readout = region.locator('[data-zoom]')
  const requestsBeforeZoom = requests
  assert.equal(await readout.first().textContent(), '1.0×')
  assert.equal(await zoomOut.getAttribute('aria-disabled'), 'true', 'zoom out is greyed out at 1x')
  await zoomIn.click()
  assert.equal(await readout.first().textContent(), '1.5×')
  near((await zoomOf()).scale, 1.5, 0.01, 'image scale 1.5')
  await zoomIn.click()
  assert.equal(await readout.first().textContent(), '2.0×')
  near((await zoomOf()).scale, 2, 0.01, 'image scale 2')
  assert.equal(await region.getByRole('button', { name: 'Zoom in (maximum reached)', exact: true }).getAttribute('aria-disabled'), 'true', 'zoom in is greyed out at 2x')
  await region.getByRole('button', { name: 'Zoom in (maximum reached)', exact: true }).click({ force: true })
  near((await zoomOf()).scale, 2, 0.01, 'never above 2x')
  const view = region.getByRole('application', { name: /^Image section, zoom 2\.0×$/ })
  await view.waitFor()
  // The zoom buttons look small (32 px) but react to a finger like every other control: 44 x 44 px.
  for (const [name, target] of [['zoom in', zoomIn], ['zoom out', zoomOut], ['reset zoom', reset]]) {
    const hit = await hitArea(target)
    assert.ok(hit.w >= 43 && hit.h >= 43, `${name}: the area that reacts is ${hit.w} x ${hit.h} px, at least 44 x 44 expected`)
  }
  // Dragging the picture pans it, bounded by the frame.
  const frame = await region.locator('.cc2-camera-frame').boundingBox()
  await page.mouse.move(frame.x + frame.width / 2, frame.y + frame.height / 2)
  await page.mouse.down()
  await page.mouse.move(frame.x + frame.width / 2 + 30, frame.y + frame.height / 2 + 20, { steps: 4 })
  await page.mouse.up()
  let zoom = await zoomOf()
  near(zoom.x, 30, 2, 'drag pans horizontally'); near(zoom.y, 20, 2, 'drag pans vertically')
  await page.mouse.move(frame.x + frame.width / 2, frame.y + frame.height / 2)
  await page.mouse.down()
  await page.mouse.move(frame.x + frame.width / 2 + 2000, frame.y + frame.height / 2 + 2000, { steps: 4 })
  await page.mouse.up()
  zoom = await zoomOf()
  near(zoom.x, frame.width / 2, 2, 'pan is limited to the picture edge'); near(zoom.y, frame.height / 2, 2, 'pan is limited to the picture edge')
  // Reset: button, double click, key 0.
  await reset.click()
  zoom = await zoomOf(); near(zoom.scale, 1, 0.01, 'reset button'); near(zoom.x, 0, 0.5, 'reset button recentres')
  await zoomIn.click()
  await region.locator('.cc2-camera-frame').dblclick()
  near((await zoomOf()).scale, 1, 0.01, 'double click resets')
  await zoomIn.click(); await zoomIn.click()
  await view.focus()
  await page.keyboard.press('0')
  near((await zoomOf()).scale, 1, 0.01, 'key 0 resets')
  await page.keyboard.press('+')
  near((await zoomOf()).scale, 1.5, 0.01, 'key + zooms in')
  await page.keyboard.press('=')
  near((await zoomOf()).scale, 2, 0.01, 'key = zooms in')
  await page.keyboard.press('+')
  near((await zoomOf()).scale, 2, 0.01, 'key + stops at 2x')
  await page.keyboard.press('ArrowLeft')
  near((await zoomOf()).x, 16, 1, 'arrow key pans 16 px')
  await page.keyboard.press('-')
  near((await zoomOf()).scale, 1.5, 0.01, 'key - zooms out')
  await page.keyboard.press('0')
  // Mouse wheel over the picture zooms (and stops at 1x and 2x); the page itself does not scroll for it.
  await page.mouse.move(frame.x + frame.width / 2, frame.y + frame.height / 2)
  await page.mouse.wheel(0, -400)
  zoom = await zoomOf(); assert.ok(zoom.scale > 1.2 && zoom.scale <= 2.001, `wheel up zooms in (got ${zoom.scale})`)
  await page.mouse.wheel(0, -4000)
  near((await zoomOf()).scale, 2, 0.01, 'wheel stops at 2x')
  await page.mouse.wheel(0, 8000)
  near((await zoomOf()).scale, 1, 0.01, 'wheel stops at 1x')
  // The wheel zooms around the pointer: the spot under it stays under it (here 60 px right of and 30 px below the centre).
  await page.mouse.move(frame.x + frame.width / 2 + 60, frame.y + frame.height / 2 + 30)
  await page.mouse.wheel(0, -300)
  zoom = await zoomOf()
  assert.ok(zoom.scale > 1.3 && zoom.scale < 2, `wheel zoom is partial (got ${zoom.scale})`)
  near(zoom.x, 60 * (1 - zoom.scale), 2, 'wheel zoom keeps the spot under the pointer (x)')
  near(zoom.y, 30 * (1 - zoom.scale), 2, 'wheel zoom keeps the spot under the pointer (y)')
  await page.mouse.wheel(0, 8000)
  near((await zoomOf()).scale, 1, 0.01, 'back to 1x')
  assert.equal(requests, requestsBeforeZoom, 'zooming must not touch the camera')
  assert.equal(clients, 1, 'zooming keeps one stream')
  // Zoom survives a resize, and is reset when the window is closed.
  await zoomIn.click()
  await size.focus()
  await page.keyboard.press('ArrowRight')
  near((await zoomOf()).scale, 1.5, 0.01, 'zoom stays when the size changes')

  // 8b. Moving, resizing and panning draw only the mini player, not the whole app (60 pointer steps each; before the
  // geometry had a store of its own, every step drew the app and the camera card once more).
  await countDraws()
  const steps = 60
  const drag = async (from, step) => {
    await page.mouse.move(from[0], from[1])
    await page.mouse.down()
    for (let i = 1; i <= steps; i++) await page.mouse.move(from[0] + step[0] * i, from[1] + step[1] * i)
    await page.mouse.up()
  }
  const drawn = async (label, action, expectedMini) => {
    const first = await draws()
    await action()
    const last = await draws()
    const app = last.app - first.app, card = last.card - first.card, mini = last.mini - first.mini
    // The printer poll (every 1.5 s) draws the app now and then on its own: a few draws are normal, not one per step.
    assert.ok(app <= 4, `${label}: the app was drawn ${app} times for ${steps} steps`)
    assert.ok(card <= 4, `${label}: the camera card was drawn ${card} times for ${steps} steps`)
    assert.ok(mini >= expectedMini, `${label}: the mini player itself must follow the pointer (${mini} draws)`)
  }
  const frameBox = await region.locator('.cc2-camera-frame').boundingBox()
  await drawn('panning', () => drag([frameBox.x + frameBox.width / 2, frameBox.y + frameBox.height / 2], [1, 0.5]), steps / 2)
  const gripNow = await grip.boundingBox()
  await drawn('moving', () => drag([gripNow.x + gripNow.width / 2, gripNow.y + gripNow.height / 2], [-3, -2]), steps / 2)
  const cornerNow = await size.boundingBox()
  await drawn('resizing', () => drag([cornerNow.x + 6, cornerNow.y + 6], [-1, -1]), steps / 2)
  await zoomOut.click() // 1.5x -> 1x

  // 9. A hidden tab pauses the stream like the camera card does, and showing it again resumes it.
  await page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { configurable: true, value: true })
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await wait(() => clients === 0, 'a hidden tab pauses the mini player')
  assert.equal(await region.count(), 1, 'the window stays while the tab is hidden')
  await page.evaluate(() => {
    delete document.hidden
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await wait(() => clients === 1, 'showing the tab resumes the stream')

  // 9b. A tab that comes back must not take the camera from somebody who started it meanwhile: the mini player checks
  // the owner first and closes without a notice, and the other viewer keeps its stream.
  await page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { configurable: true, value: true })
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await wait(() => clients === 0, 'the hidden tab pauses the mini player again')
  const rival = await context.newPage()
  rival.on('pageerror', e => errors.push(e.message))
  await rival.goto(`${origin}/#dashboard`)
  await rival.getByRole('button', { name: 'Start live view', exact: true }).click()
  await rival.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  await wait(() => clients === 1, 'the other viewer streams while the mini player is paused')
  await page.evaluate(() => {
    delete document.hidden
    document.dispatchEvent(new Event('visibilitychange'))
  })
  await wait(() => region.count().then(n => n === 0), 'a tab shown again closes the mini player when another viewer has the camera')
  await observe(2000)
  assert.equal(clients, 1, 'the mini player did not take the camera back; only the other viewer streams')
  assert.equal(await rival.locator('.cc2-camera-frame img').count(), 1, 'the other viewer keeps its picture')
  assert.equal(await page.getByText('Live view is open in another window or on another device', { exact: true }).count(), 0, 'closing quietly shows no hint')
  await rival.close()
  await wait(() => clients === 0, 'the other viewer is gone')
  await open.click()
  await region.waitFor()
  await wait(() => clients === 1, 'reopened after the other viewer')

  // 10. The printer state is read every 1.5 s while the mini player streams, also on a page that reads it only every
  // 3 s (Settings), so a takeover is noticed quickly. Measured as the longest gap between two reads over 7 s.
  await link('Settings').click()
  await page.waitForFunction(() => location.hash === '#settings')
  await observe(500) // let the route change and its own requests settle
  const from = Date.now()
  await observe(7000)
  const until = Date.now()
  const times = [from, ...printerReadTimes.filter(t => t >= from && t <= until), until]
  const longest = Math.max(...times.slice(1).map((t, i) => t - times[i]))
  assert.ok(longest < 2300, `the printer state is read every 1.5 s while the mini player streams (longest gap ${longest} ms; ${times.length - 2} reads in 7 s)`)
  await link('Dashboard').click()
  await page.waitForFunction(() => location.hash === '#dashboard')

  // 11. Another viewer closes ONLY the mini player: the Camera button on the Job page, the Camera card and the camera window.
  await link('Job').click()
  await jobCamera.click()
  await wait(() => region.count().then(n => n === 0), 'the Camera button closes the mini player')
  await wait(() => clients === 1, 'only the job page streams')
  await observe(2500)
  assert.equal(clients, 1, 'the job camera keeps running, nothing reconnects')
  assert.equal(await page.getByText('Live view is open in another window or on another device', { exact: true }).count(), 0, 'closing for another viewer shows no hint')
  assert.equal(await page.locator('.cc2-job-camera img').count(), 1, 'the job page picture is still there')
  await jobCamera.click() // camera off again
  await wait(() => clients === 0, 'the job camera closes')
  await page.evaluate(() => localStorage.removeItem('cc2-job-camera'))
  await link('Dashboard').click()
  // Reopen, then start the dashboard card itself: the mini player closes, the card streams.
  await open.click()
  await region.waitFor()
  await wait(() => clients === 1, 'reopened')
  const reopened = await zoomOf()
  near(reopened.scale, 1, 0.01, 'a reopened mini player starts at 1x')
  const other = await context.newPage()
  other.on('pageerror', e => errors.push(e.message))
  await other.goto(`${origin}/#dashboard`)
  await other.getByRole('button', { name: 'Start live view', exact: true }).click()
  await other.locator('.cc2-camera-frame img').waitFor({ state: 'visible' })
  await wait(() => region.count().then(n => n === 0), 'another browser page closes the mini player')
  await wait(() => clients === 1, 'only the other page streams')
  await observe(2500)
  assert.equal(clients, 1, 'a closed mini player does not come back and steal the camera')
  // The separate camera window is the same: opening the mini player takes the camera from it, which then offers Watch here.
  await open.click()
  await region.waitFor()
  await other.getByRole('button', { name: 'Watch here', exact: true }).waitFor()
  await wait(() => clients === 1, 'the mini player took the camera over')
  await other.close()

  // 12. Close: the stream ends, the window goes, and the card is usable again. The focus goes back to the card's
  // Mini player button (it would fall to the start of the page otherwise).
  await region.getByRole('button', { name: 'Close mini player', exact: true }).click()
  await wait(() => region.count().then(n => n === 0), 'close removes the window')
  await wait(() => clients === 0, 'close ends the stream')
  assert.equal(await page.evaluate(() => document.activeElement?.hasAttribute('data-miniplayer-open')), true, 'after closing, the focus is on the Mini player button of the card')
  // Escape closes it from inside the window only, and gives the focus back the same way.
  await open.click()
  await region.waitFor()
  await wait(() => clients === 1, 'opened for the Escape check')
  await open.focus()
  await page.keyboard.press('Escape')
  await observe(300)
  assert.equal(await region.count(), 1, 'Escape outside the window does not close it')
  await region.getByRole('button', { name: 'Move mini player', exact: true }).focus()
  await page.keyboard.press('Escape')
  await wait(() => region.count().then(n => n === 0), 'Escape inside the window closes it')
  await wait(() => clients === 0, 'Escape ends the stream')
  assert.equal(await page.evaluate(() => document.activeElement?.hasAttribute('data-miniplayer-open')), true, 'after Escape, the focus is on the Mini player button of the card')
  assert.equal(await region.count(), 0)

  // 12b. The card was streaming when the mini player opens: the card stops, shows where its picture went, and there is
  // still exactly one stream; nothing offers "Watch here". Closing the mini player removes the hint.
  const inMini = page.getByText('Live view is open in the mini player', { exact: true })
  await page.getByRole('button', { name: 'Start live view', exact: true }).click()
  await page.locator('.cc2-camera-card img').waitFor({ state: 'visible' })
  await wait(() => clients === 1, 'the card streams')
  assert.equal(await inMini.count(), 0, 'no hint while the card has the picture itself')
  await open.click()
  await region.waitFor()
  await inMini.waitFor()
  await wait(() => clients === 1 && requests > 0, 'one stream after the move')
  await observe(800)
  assert.equal(clients, 1, 'the card and the mini player never stream at once')
  assert.equal(await page.locator('.cc2-camera-card img').count(), 0, 'the card has given its picture up')
  assert.equal(await page.getByRole('button', { name: 'Watch here', exact: true }).count(), 0, 'the card does not offer Watch here for its own mini player')
  await region.getByRole('button', { name: 'Close mini player', exact: true }).click()
  await wait(() => clients === 0, 'closing the mini player ends the stream')
  await wait(() => inMini.count().then(n => n === 0), 'the hint goes with the mini player')
  await page.getByRole('button', { name: 'Start live view', exact: true }).waitFor({ state: 'visible' })

  // 13. A failing camera keeps the window open with the usual retry hint.
  reject = true
  await open.click()
  await region.waitFor()
  await region.getByRole('button', { name: 'Retry now', exact: true }).waitFor()
  assert.equal(await region.count(), 1, 'a failing camera does not close the window')
  // Screen readers are told: a live region of the window carries the state while there is no picture.
  assert.match(await region.getByRole('status').textContent(), /Camera stream unavailable|Retrying|gave up|Waiting for camera stream/i, 'the state is announced')
  reject = false
  await region.getByRole('button', { name: 'Retry now', exact: true }).click()
  await wait(() => clients === 1, 'retry recovers')
  await wait(async () => (await region.getByRole('status').textContent()) === '', 'the announcement is empty while the picture is there')
  assert.ok(await region.getByRole('button', { name: 'Move mini player', exact: true }).getAttribute('aria-keyshortcuts'), 'the grip names its arrow keys')
  await region.getByRole('button', { name: 'Close mini player', exact: true }).click()
  await wait(() => clients === 0, 'closed')

  await page.close()

  // 14. Phone: limits are 160 px to the window width minus 24 px; the grip and the corner work with touch.
  const phoneContext = await newContext({ width: 393, height: 780 }, { hasTouch: true, isMobile: true, deviceScaleFactor: 2 })
  const phone = await phoneContext.newPage()
  phone.on('pageerror', e => errors.push(e.message))
  await phone.goto(`${origin}/#dashboard`)
  await phone.addStyleTag({ content: '#cc2-preview-banner{display:none!important}' }) // the demo's own bar lies over the bottom of a phone screen; it is not part of the product
  await phone.getByRole('button', { name: 'Mini player', exact: true }).click()
  const pRegion = phone.getByRole('region', { name: 'Mini player live camera', exact: true })
  await pRegion.waitFor()
  await wait(() => clients === 1, 'phone: one stream')
  const pSize = pRegion.getByRole('slider', { name: 'Mini player size', exact: true })
  assert.equal(await pSize.getAttribute('aria-valuemin'), '160')
  assert.equal(await pSize.getAttribute('aria-valuemax'), '369')
  await pSize.focus()
  await phone.keyboard.press('End')
  let pb = await pRegion.boundingBox()
  near(pb.width, 369, 1, 'phone maximum')
  assert.ok(pb.x >= 0 && pb.x + pb.width <= 393, 'phone: inside the screen')
  await phone.keyboard.press('Home')
  pb = await pRegion.boundingBox()
  near(pb.width, 160, 1, 'phone minimum')
  // At the minimum there is no room for the grip (the whole bar moves the window) and the close button sits on the picture.
  assert.equal(await pRegion.getByRole('button', { name: 'Move mini player', exact: true }).isVisible(), false, 'phone minimum: no grip button, the bar itself moves the window')
  for (const [targetName, target] of [['size', pSize], ['close', pRegion.getByRole('button', { name: 'Close mini player', exact: true })], ['pause', pRegion.getByRole('button', { name: 'Pause', exact: true })]]) {
    const hit = await hitArea(target)
    assert.ok(hit.w >= 43 && hit.h >= 43, `phone minimum: ${targetName} touch target ${hit.w} x ${hit.h} must be at least 44 x 44`)
  }
  await phone.keyboard.press('End') // room for the grip again
  // Targets for fingers are at least 44 x 44 px.
  for (const target of [pRegion.getByRole('button', { name: 'Move mini player', exact: true }), pSize, pRegion.getByRole('button', { name: 'Close mini player', exact: true }), pRegion.getByRole('button', { name: 'Pause', exact: true })]) {
    const hit = await target.evaluate(el => {
      const r = el.getBoundingClientRect()
      const before = getComputedStyle(el, '::before')
      const grow = (Number.parseFloat(before.width) || 0) > r.width ? Number.parseFloat(before.width) : r.width
      const growH = (Number.parseFloat(before.height) || 0) > r.height ? Number.parseFloat(before.height) : r.height
      return { w: grow, h: growH }
    })
    assert.ok(hit.w >= 44 && hit.h >= 44, `touch target ${JSON.stringify(hit)} must be at least 44 x 44`)
  }
  // At the smallest width (160 px) the bar has room for 32 px per zoom button side by side: full height, narrower.
  await phone.keyboard.press('Home')
  for (const name of [/^Zoom in/, /^Zoom out/]) {
    const hit = await hitArea(pRegion.getByRole('button', { name }))
    assert.ok(hit.h >= 43 && hit.w >= 31, `phone minimum: a zoom button reacts on ${hit.w} x ${hit.h} px`)
  }
  await phone.keyboard.press('End')
  // Move with a finger on the grip.
  const cdp = await phoneContext.newCDPSession(phone)
  const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map((p, id) => ({ x: p[0], y: p[1], id })) })
  pb = await pRegion.boundingBox()
  const gb = await pRegion.getByRole('button', { name: 'Move mini player', exact: true }).boundingBox()
  const g = [gb.x + gb.width / 2, gb.y + gb.height / 2]
  await touch('touchStart', [g])
  for (let i = 1; i <= 6; i++) await touch('touchMove', [[g[0] + 8 * i, g[1] - 40 * i]])
  await touch('touchEnd', [])
  const pm = await pRegion.boundingBox()
  assert.ok(pm.y < pb.y - 150, `a finger drag on the grip moves it up (${pb.y} -> ${pm.y})`)
  assert.ok(pm.x >= 0 && pm.x + pm.width <= 393 && pm.y >= 0, 'phone: still inside the screen')
  assert.equal(await phone.evaluate(() => window.scrollY), 0, 'moving with a finger does not scroll the page')
  // Two fingers zoom the picture, one finger pans it, a double tap resets it.
  await pSize.focus()
  await phone.keyboard.press('End')
  const pf = await pRegion.locator('.cc2-camera-frame').boundingBox()
  const c = [pf.x + pf.width / 2, pf.y + pf.height / 2]
  const pinchZoom = async () => (await phone.evaluate(() => new DOMMatrix(getComputedStyle(document.querySelector('.cc2-miniplayer img')).transform).a))
  await touch('touchStart', [[c[0] - 30, c[1]], [c[0] + 30, c[1]]])
  for (let i = 1; i <= 6; i++) await touch('touchMove', [[c[0] - 30 - 8 * i, c[1]], [c[0] + 30 + 8 * i, c[1]]])
  await touch('touchEnd', [])
  const pinched = await pinchZoom()
  assert.ok(pinched > 1.3 && pinched <= 2.001, `two fingers zoom in (got ${pinched})`)
  await touch('touchStart', [c]); await touch('touchEnd', [])
  await observe(80)
  await touch('touchStart', [c]); await touch('touchEnd', [])
  await wait(async () => Math.abs((await pinchZoom()) - 1) < 0.01, 'a double tap resets the zoom')
  assert.equal(clients, 1, 'phone: gestures keep one stream')
  // 15. The side drawer covers the mini player instead of the other way round (a phone's menu is above the mini player).
  await phone.locator('#cc2-menu-toggle').click()
  await phone.locator('.cc2-sidebar-open').waitFor()
  const covered = await phone.evaluate(() => {
    const r = document.querySelector('.cc2-miniplayer').getBoundingClientRect()
    const hit = document.elementFromPoint(Math.min(r.right - 4, innerWidth - 4), r.top + 8)
    return Boolean(hit && !hit.closest('.cc2-miniplayer'))
  })
  assert.equal(covered, true, 'the open menu is above the mini player')
  await phoneContext.close()

  assert.deepEqual(softFailures, [], 'layout and hand-over checks failed')
  assert.deepEqual(errors, [])
  console.log('PASS: mini player opens one stream, stays across pages, resizes 240-640 px (160-369 on a phone) in 16:9, moves with mouse, keys and finger, remembers size and position, zooms 1-2x with buttons, keys, wheel, drag and two fingers, pauses in a hidden tab, closes alone when another viewer takes the camera')
} finally {
  await browser?.close()
  await server.close()
  stream.closeAllConnections()
  await new Promise(r => stream.close(r))
}
