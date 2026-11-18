// Bed map on the Job page: Live from the Objects tab, selecting and deselecting objects on both tabs, the number
// badge and status marks on the Layer path tab, and the object preview (a rough drawing of the selected object
// read from the G-code): loading, refusals, retry on "busy", one request per object and page, narrow width.
import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { fixture, launchOptions } from './fixture.mjs'

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
const A = 'Buddha.stl_id_0_copy_0' // polygon, bottom left
const B = 'Buddha.stl_id_1_copy_0' // polygon, bottom right (excluded in some steps)
const C = 'Buddha.stl_id_2_copy_0' // centre only
const D = 'Buddha.stl_id_3_copy_0' // centre only
const E = 'Buddha.stl_id_4_copy_0' // centre only
const F = 'Buddha.stl_id_5_copy_0' // centre only
const G = 'Buddha.stl_id_6_copy_0' // centre only
const H = 'Buddha.stl_id_7_copy_0' // centre only
// A fresh page: a new module cache, so no preview is remembered, and the same eight objects on every start.
// With clock: true the page runs on Playwright's fake clock, so waits of many seconds can be skipped with runFor().
async function start(width = 1440, { clock = false } = {}) {
  const page = await browser.newPage({ viewport: { width, height: 1000 } })
  if (clock) await page.clock.install()
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":"en"}'))
  await page.goto(demo.url + '#job')
  await page.evaluate(
    ({ A, B, C, D, E, F, G, H }) => {
      const state = window.__CC2_PREVIEW
      Object.assign(state, {
        status: 2, filename: 'preview.gcode', current: C, excluded: [], layer: 10, totalLayers: 227,
        layerBusy: false, layerHeld: false, layerCalls: 0, previewUrls: [], previewStamps: [], previewFail: [], previewBusy: false,
        previewHeld: false, previewGate: null,
      })
      state.objectRows = [
        { name: A, polygon: [[30, 30], [70, 30], [70, 70], [30, 70]] },
        { name: B, polygon: [[190, 40], [230, 40], [230, 80], [190, 80]] },
        { name: C, center: [60, 200] },
        { name: D, center: [200, 200] },
        { name: E, center: [128, 40] },
        { name: F, center: [230, 130] },
        { name: G, center: [100, 230] },
        { name: H, center: [30, 130] },
      ]
      const original = window.fetch
      const json = (body, status = 200) =>
        new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } })
      // The read hold: the printer homes, levels, probes or starts a print, and the service reads no file.
      const held = () => json({ error: 'Printer is homing or starting a print; retry shortly' }, 503)
      window.fetch = (url, opts = {}) => {
        const u = String(url)
        if (u.includes('/api/printer'))
          return original(url, opts).then(async r => {
            const d = await r.json()
            d.print.filename = state.filename
            d.print.current_layer = state.layer
            d.print.total_layers = state.totalLayers
            d.print.duration = 600
            d.motion = { ...d.motion, x: 128, y: 128, z: state.layer * 0.2 }
            return json(d)
          })
        if (u.includes('/api/exclude-objects'))
          return Promise.resolve(
            json({ result: { status: { exclude_object: { objects: state.objectRows, current_object: state.current, excluded_objects: state.excluded } } } })
          )
        if (u.includes('/api/gcode-files/layer-path')) {
          state.layerCalls++
          if (state.layerHeld) return Promise.resolve(held())
          if (state.layerBusy) return Promise.resolve(json({ error: 'Layer path busy; retry shortly' }, 503))
          return original(url, opts)
        }
        if (u.includes('/api/gcode-files/object-preview')) {
          state.previewUrls.push(u)
          state.previewStamps.push(Date.now())
          if (state.previewHeld) return Promise.resolve(held())
          if (state.previewBusy) return Promise.resolve(json({ error: 'Object preview busy; retry shortly' }, 503))
          const fail = state.previewFail.shift()
          if (fail) return Promise.resolve(json({ error: fail.error || 'busy' }, fail.status))
          if (state.previewGate) return state.previewGate.then(() => original(url, opts))
          return original(url, opts)
        }
        return original(url, opts)
      }
    },
    { A, B, C, D, E, F, G, H }
  )

  const card = page.locator('section:has(.cc2-job-bar)')
  const map = card.locator('[data-bed-map]')
  const live = card.locator('[data-layer-live]')
  const badge = id => card.locator(`[data-bed-object-id="${id}"]`)
  const outline = id => card.locator(`[data-bed-outline-id="${id}"]`)
  const listItem = id => card.locator(`[data-bed-list-id="${id}"]`)
  const preview = id => card.locator(`[data-object-preview="${id}"]`)
  const anyPreview = card.locator('[data-object-preview]')
  const status = card.locator('[data-layer-status]')
  const selection = card.locator('[data-bed-selection]')
  const mapTab = () => card.locator('[data-bed-map-tab]').getAttribute('data-bed-map-tab')
  const set = values => page.evaluate(v => Object.assign(window.__CC2_PREVIEW, v), values)
  const previewUrls = () => page.evaluate(() => [...window.__CC2_PREVIEW.previewUrls])
  const showTab = async name => {
    await card.getByRole('button', { name: name === 'layer' ? 'Layer path' : 'Objects', exact: true }).click()
    await page.waitForFunction(n => document.querySelector('[data-bed-map-tab]')?.getAttribute('data-bed-map-tab') === n, name)
  }
  const none = async () => {
    // Nothing is selected: every number, outline and list row is unmarked and the text asks for a choice.
    for (const id of [A, B, C, D, E, F, G, H]) {
      assert.equal(await badge(id).getAttribute('aria-pressed'), 'false', `${id} is not selected`)
      assert.equal(await badge(id).getAttribute('data-selected'), null)
      assert.equal(await listItem(id).getAttribute('aria-pressed'), 'false')
    }
    assert.equal(await outline(A).getAttribute('data-selected'), null)
    assert.match(await selection.innerText(), /^Select an object/)
    assert.equal(await anyPreview.count(), 0, 'no preview without a selection')
  }
  const only = async id => {
    for (const other of [A, B, C, D, E, F, G, H].filter(x => x !== id))
      assert.equal(await badge(other).getAttribute('aria-pressed'), 'false', `${other} stays unselected`)
    assert.equal(await badge(id).getAttribute('aria-pressed'), 'true')
    assert.equal(await badge(id).getAttribute('data-selected'), 'true')
    assert.equal(await listItem(id).getAttribute('aria-pressed'), 'true')
    assert.ok((await selection.innerText()).includes('Buddha'), 'the selection text names the object')
  }
  await page.waitForFunction(() => document.querySelectorAll('[data-bed-object-id]').length === 8)
  assert.equal(await mapTab(), 'objects')
  return { page, errors, card, map, live, badge, outline, listItem, preview, anyPreview, status, selection, mapTab, set, previewUrls, showTab, none, only }
}

try {
  const { page, errors, card, map, live, badge, outline, listItem, preview, anyPreview, status, selection, mapTab, set, previewUrls, showTab, none, only } = await start()

  // --- 1. Live on the Objects tab ---------------------------------------------------------------------------------
  assert.equal(await live.isEnabled(), true, 'Live can be pressed on the Objects tab during a print')
  assert.equal(await live.getAttribute('aria-pressed'), 'false', 'but it is not lit there')
  await live.click()
  assert.equal(await mapTab(), 'layer', 'Live on the Objects tab opens the Layer path')
  assert.equal(await live.getAttribute('aria-pressed'), 'true', 'and follows the printer')
  await page.waitForFunction(() => /Layer 10 \/ 227/.test(document.querySelector('[data-layer-status]')?.textContent || '') && document.querySelectorAll('[data-layer-path]').length > 0)
  // Step away from the live layer, return to the Objects tab, and press Live there: it follows the printer again.
  await card.getByRole('button', { name: 'Previous layer' }).click()
  await page.waitForFunction(() => /Layer 9 \/ 227/.test(document.querySelector('[data-layer-status]')?.textContent || ''))
  assert.equal(await live.getAttribute('aria-pressed'), 'false')
  await showTab('objects')
  assert.equal(await live.isEnabled(), true)
  await live.click()
  assert.equal(await mapTab(), 'layer')
  await page.waitForFunction(() => /Layer 10 \/ 227/.test(document.querySelector('[data-layer-status]')?.textContent || ''))
  assert.equal(await live.getAttribute('aria-pressed'), 'true')
  await showTab('objects')
  // No print: Live stays locked on the Objects tab, and a click changes nothing.
  await set({ status: 1 })
  await page.waitForFunction(() => document.querySelector('[data-layer-live]')?.disabled)
  assert.equal(await live.isDisabled(), true, 'Live is locked without a print')
  assert.equal(await mapTab(), 'objects')
  await set({ status: 2 })
  await page.waitForFunction(() => document.querySelectorAll('[data-bed-object-id]').length === 8 && !document.querySelector('[data-layer-live]')?.disabled)
  // No file: a print without a file name counts as no print, so Live is locked as well.
  await set({ filename: '' })
  await page.waitForFunction(() => document.querySelector('[data-layer-live]')?.disabled)
  assert.equal(await live.isDisabled(), true, 'Live is locked without a file')
  await set({ filename: 'preview.gcode' })
  await page.waitForFunction(() => document.querySelectorAll('[data-bed-object-id]').length === 8 && !document.querySelector('[data-layer-live]')?.disabled)

  // --- 2 and 3. Select and deselect on both tabs; same marks and badge look on both -------------------------------
  await set({ excluded: [B] })
  await page.waitForFunction(b => document.querySelector(`[data-bed-object-id="${b}"]`)?.disabled, B)
  const look = el =>
    el.evaluate(e => {
      const s = getComputedStyle(e)
      const r = e.getBoundingClientRect()
      return {
        w: Math.round(r.width), h: Math.round(r.height), bg: s.backgroundColor, color: s.color,
        border: `${s.borderTopColor} ${s.borderTopWidth} ${s.borderTopStyle}`, radius: s.borderTopLeftRadius,
        size: s.fontSize, weight: s.fontWeight, opacity: s.opacity, deco: s.textDecorationLine,
        outline: `${s.outlineStyle} ${s.outlineColor} ${s.outlineWidth}`, shadow: s.boxShadow,
      }
    })
  const looks = {}
  for (const tab of ['objects', 'layer']) {
    await showTab(tab)
    if (tab === 'layer') await page.waitForFunction(() => document.querySelectorAll('[data-layer-path]').length > 0)
    await none()
    // Number: select, select again = deselect.
    await badge(A).click()
    await only(A)
    assert.equal(await outline(A).getAttribute('data-selected'), 'true', `${tab}: the outline of the selected object is marked`)
    await badge(A).click()
    await none()
    // Outline (the whole object area is the click target).
    await outline(A).click({ position: { x: 6, y: 6 } })
    await only(A)
    await outline(A).click({ position: { x: 6, y: 6 } })
    await none()
    // List.
    await listItem(C).click()
    await only(C)
    assert.equal(await outline(A).getAttribute('data-selected'), null)
    await listItem(C).click()
    await none()
    // The three ways share one selection: select by number, switch by list, clear by outline of another object is not
    // possible (C has no outline), so clear by number.
    await badge(A).click()
    await listItem(D).click()
    await only(D)
    assert.equal(await outline(A).getAttribute('data-selected'), null, 'selecting another object moves the mark')
    await badge(D).click()
    await none()
    // Excluded objects cannot be selected, and a click on their outline does nothing.
    assert.equal(await badge(B).isDisabled(), true, `${tab}: the excluded number is disabled`)
    assert.equal(await listItem(B).isDisabled(), true, `${tab}: the excluded list row is disabled`)
    assert.equal(await outline(B).evaluate(el => el.classList.contains('cc2-object-map-outline-pick')), false, 'the excluded outline is not a click target')
    await outline(B).dispatchEvent('click')
    await none()
    // Marks on number and outline: selected (A), current (C has no outline, so the number only), excluded (B).
    await badge(A).click()
    assert.equal(await badge(C).getAttribute('data-current'), 'true', `${tab}: the current number is marked`)
    assert.equal(await badge(A).getAttribute('data-current'), null)
    assert.equal(await badge(B).getAttribute('data-excluded'), 'true', `${tab}: the excluded number is marked`)
    assert.equal(await outline(B).getAttribute('data-excluded'), 'true', `${tab}: the excluded outline is marked`)
    assert.equal(await outline(A).getAttribute('data-excluded'), null)
    assert.equal(await badge(A).getAttribute('data-excluded'), null)
    looks[tab] = { selected: await look(badge(A)), current: await look(badge(C)), excluded: await look(badge(B)), plain: await look(badge(D)) }
    // The legend names the status colours on both tabs; the nozzle only belongs to the Layer path.
    const legend = await card.locator('.cc2-object-map-legend').innerText()
    assert.match(legend, /Selected[\s\S]*Current Object[\s\S]*Excluded/, `${tab}: legend names selected, current and excluded`)
    if (tab === 'layer') assert.match(legend, /Layer path[\s\S]*Nozzle/)
    else assert.doesNotMatch(legend, /Nozzle/)
    // Badge geometry.
    const b = await badge(A).boundingBox()
    assert.deepEqual([Math.round(b.width), Math.round(b.height)], [32, 32], `${tab}: the number is 32 x 32 px`)
    assert.equal(await badge(A).getAttribute('data-layer-badge'), tab === 'layer' ? 'true' : null)
    const m = await map.boundingBox()
    const px = mm => (mm / 256) * m.width
    const py = mm => m.height - (mm / 256) * m.height
    // Object A spans x 30..70, y 30..70, centre (50, 50).
    if (tab === 'objects') {
      assert.ok(Math.abs(b.x + b.width / 2 - (m.x + px(50))) < 1.5 && Math.abs(b.y + b.height / 2 - (m.y + py(50))) < 1.5, 'Objects tab: the number is in the middle of the object')
    } else {
      assert.ok(Math.abs(b.x + b.width - (m.x + px(30 - 1.5))) < 1.5, `Layer path: the right edge of the number sits at the anchor, left of the object: ${b.x + b.width} vs ${m.x + px(28.5)}`)
      assert.ok(Math.abs(b.y + b.height / 2 - (m.y + py(50))) < 1.5, 'Layer path: the number is level with the middle of the object')
      const shape = await outline(A).boundingBox()
      assert.ok(b.x + b.width <= shape.x + 0.5, 'Layer path: the number does not cover the object')
    }
    await badge(A).click()
    await none()
  }
  for (const kind of ['selected', 'current', 'excluded', 'plain'])
    assert.deepEqual(looks.layer[kind], looks.objects[kind], `a ${kind} number looks the same on both tabs`)
  assert.deepEqual([looks.layer.plain.w, looks.layer.plain.h], [32, 32])
  // The marks follow the data, not the tab: the current object changes and the mark moves on both tabs.
  for (const tab of ['layer', 'objects']) {
    await showTab(tab)
    await set({ current: A })
    await page.waitForFunction(a => document.querySelector(`[data-bed-object-id="${a}"]`)?.getAttribute('data-current') === 'true', A)
    assert.equal(await outline(A).getAttribute('data-current'), 'true', `${tab}: the current outline is marked`)
    assert.equal(await badge(C).getAttribute('data-current'), null)
    await set({ current: C })
    await page.waitForFunction(c => document.querySelector(`[data-bed-object-id="${c}"]`)?.getAttribute('data-current') === 'true', C)
    assert.equal(await outline(A).getAttribute('data-current'), null)
  }
  await set({ excluded: [] })
  await page.waitForFunction(b => !document.querySelector(`[data-bed-object-id="${b}"]`)?.disabled, B)

  // --- 4. Object preview ------------------------------------------------------------------------------------------
  await showTab('objects')
  assert.equal(await anyPreview.count(), 0)
  await badge(A).click()
  await preview(A).waitFor()
  await page.waitForFunction(a => document.querySelector(`[data-object-preview="${a}"] svg`), A)
  assert.equal(await preview(A).locator('svg').getAttribute('data-object-preview-rings'), '20')
  assert.equal(await preview(A).locator('svg .cc2-object-preview-line').count(), 20, 'one line per drawn layer')
  assert.equal(await preview(A).locator('[data-object-preview-status]').count(), 0, 'no status text beside the drawing')
  assert.equal(await preview(A).locator('[data-object-preview-note]').innerText(), 'Rough shape: 20 of 60 layers, outer walls only')
  assert.match(await preview(A).innerText(), /Object preview/)
  assert.equal(await preview(A).locator('svg').getAttribute('role'), 'img')
  assert.match(await preview(A).locator('svg').getAttribute('aria-label'), /Object preview: /)
  // Selecting A, C and D on both tabs and by number, outline and list (section 2) asked once for each of them.
  const urls = await previewUrls()
  const queryOf = u => new URL(u, 'http://x').searchParams
  assert.deepEqual(urls.map(u => queryOf(u).get('object')), [A, C, D], 'one request per object and page, however often and wherever it was selected')
  assert.ok(urls.every(u => new URL(u, 'http://x').pathname === '/api/gcode-files/object-preview'))
  assert.deepEqual([queryOf(urls[0]).get('storage'), queryOf(urls[0]).get('file')], ['internal', 'preview.gcode'])
  // Place: on the right of the map at desktop width, inside the right column above the selection text.
  const mapBox = await map.boundingBox()
  const previewBox = await preview(A).boundingBox()
  const selectionBox = await selection.boundingBox()
  assert.ok(previewBox.x >= mapBox.x + mapBox.width, `the preview is right of the map: ${previewBox.x} vs ${mapBox.x + mapBox.width}`)
  assert.ok(previewBox.y + previewBox.height <= selectionBox.y + 0.5, 'the preview sits above the selection text')
  // The same drawing on the Layer path tab.
  await showTab('layer')
  await page.waitForFunction(() => document.querySelectorAll('[data-layer-path]').length > 0)
  assert.equal(await preview(A).locator('svg .cc2-object-preview-line').count(), 20, 'the Layer path tab shows the preview too')
  assert.equal((await previewUrls()).length, 3, 'switching the tab asks again for nothing')
  // Deselect removes the field; selecting the same object again in the same page is not requested a second time.
  await badge(A).click()
  await anyPreview.first().waitFor({ state: 'detached' })
  await none()
  await badge(A).click()
  await preview(A).locator('svg').waitFor()
  await showTab('objects')
  await listItem(A).click()
  await anyPreview.first().waitFor({ state: 'detached' })
  await outline(A).click({ position: { x: 6, y: 6 } })
  await preview(A).locator('svg').waitFor()
  assert.equal((await previewUrls()).length, 3, 'the same object is fetched once per page, however it is selected')
  assert.equal(await page.evaluate(() => window.__objectPreviewCalls), 3, 'the service saw three requests: A, C, D')
  // Another object: its own request, the field names it, the drawing swaps.
  await page.evaluate(() => {
    const s = window.__CC2_PREVIEW
    s.previewGate = new Promise(r => (s.openGate = r))
  })
  await listItem(B).click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.previewUrls.length === 4)
  await preview(B).locator('[data-object-preview-status="loading"]').waitFor()
  assert.equal(await preview(B).locator('svg').count(), 0, 'the drawing of the first object is gone while the next one loads')
  assert.equal(await anyPreview.count(), 1, 'one field at a time')
  await page.evaluate(() => {
    const s = window.__CC2_PREVIEW
    s.previewGate = null
    s.openGate()
  })
  await preview(B).locator('svg').waitFor()
  assert.equal((await previewUrls()).length, 4)
  assert.equal(await queryOf((await previewUrls())[3]).get('object'), B)
  await badge(B).click()
  await none()

  // Loading: the field appears with a status until the answer arrives, then shows the drawing.
  await page.evaluate(() => {
    const s = window.__CC2_PREVIEW
    s.openGate = null
    s.previewGate = new Promise(r => (s.openGate = r))
  })
  await badge(E).click()
  await preview(E).waitFor()
  assert.equal(await preview(E).locator('[data-object-preview-status]').getAttribute('data-object-preview-status'), 'loading')
  assert.equal(await preview(E).locator('svg').count(), 0)
  assert.equal(await preview(E).locator('.cc2-object-preview-stage').getAttribute('aria-busy'), 'true')
  assert.match(await preview(E).locator('[data-object-preview-status]').innerText(), /Reading the file for the preview/)
  assert.equal(await preview(E).locator('[data-object-preview-note]').count(), 0, 'no note while loading')
  // The loading field already has the place of the drawing: the stage keeps its size.
  const loadingStage = await preview(E).locator('.cc2-object-preview-stage').boundingBox()
  await page.evaluate(() => {
    const s = window.__CC2_PREVIEW
    s.previewGate = null
    s.openGate()
  })
  await preview(E).locator('svg').waitFor()
  const drawnStage = await preview(E).locator('.cc2-object-preview-stage').boundingBox()
  assert.ok(Math.abs(drawnStage.height - loadingStage.height) < 1 && Math.abs(drawnStage.width - loadingStage.width) < 1, 'the drawing fills the place of the loading text: same size')
  await badge(E).click()
  await none()

  // Refusals: each shows its own text and no drawing; none counts as "cached", so a retry asks again.
  const refusals = [
    [{ status: 413, error: 'Object preview file too large' }, 'too_large', /larger than 50 MiB/],
    [{ status: 413, error: 'Object preview scan limit reached' }, 'limit', /could not be read in time/],
    [{ status: 409, error: 'Print is starting' }, 'starting', /once the print has started/],
    [{ status: 404, error: 'Object not found' }, 'missing', /Preview not available/],
  ]
  for (const tab of ['objects', 'layer']) {
    await showTab(tab)
    for (const [forced, name, text] of refusals) {
      await page.evaluate(f => (window.__objectPreview = f), forced)
      await listItem(F).click()
      const field = preview(F).locator('[data-object-preview-status]')
      await page.waitForFunction(n => document.querySelector('[data-object-preview-status]')?.getAttribute('data-object-preview-status') === n, name)
      assert.match(await field.innerText(), text, `${tab}: ${name} text`)
      assert.equal(await preview(F).locator('svg').count(), 0, `${tab}: ${name} has no drawing`)
      assert.equal(await preview(F).locator('[data-object-preview-note]').count(), 0)
      await listItem(F).click()
      await anyPreview.first().waitFor({ state: 'detached' })
    }
    await page.evaluate(() => (window.__objectPreview = null))
  }
  // After a refusal the object loads once the service answers again (the refusal was not remembered).
  const before = (await previewUrls()).length
  await listItem(F).click()
  await preview(F).locator('svg').waitFor()
  assert.equal((await previewUrls()).length, before + 1, 'a refused object is asked for again')
  await listItem(F).click()
  await none()

  // Busy (503): retried with growing waits (300 ms, 600 ms, 1.2 s, then up to 1.5 s), then the drawing comes.
  await set({ previewFail: [{ status: 503 }, { status: 503 }, { status: 503 }, { status: 503 }], previewStamps: [] })
  await badge(G).click()
  await preview(G).locator('svg').waitFor({ timeout: 15000 })
  const stamps = await page.evaluate(() => [...window.__CC2_PREVIEW.previewStamps])
  assert.equal(stamps.length, 5, `four busy answers, then one that works: ${stamps.length}`)
  const gaps = stamps.slice(1).map((t, i) => t - stamps[i])
  const wanted = [300, 600, 1200, 1500]
  gaps.forEach((gap, i) => assert.ok(gap >= wanted[i] - 40 && gap <= wanted[i] + 600, `retry ${i + 1} waits about ${wanted[i]} ms, got ${gap}: ${gaps}`))
  assert.equal(await anyPreview.count(), 1)
  await badge(G).click()
  await none()
  // Deselecting while a busy answer is pending ends the retries.
  await set({ previewBusy: true, previewStamps: [] })
  await badge(H).click()
  await page.waitForFunction(() => window.__CC2_PREVIEW.previewStamps.length >= 2)
  assert.equal(await preview(H).locator('[data-object-preview-status]').getAttribute('data-object-preview-status'), 'loading', 'a busy service keeps showing loading')
  await badge(H).click()
  await none()
  const asked = await page.evaluate(() => window.__CC2_PREVIEW.previewStamps.length)
  await page.waitForTimeout(2200)
  assert.equal(await page.evaluate(() => window.__CC2_PREVIEW.previewStamps.length), asked, 'no retries after the object was deselected')
  await set({ previewBusy: false })

  assert.deepEqual(errors, [])

  // --- 5. Busy answers of the layer path while the preview is awaited ---------------------------------------------
  // The layer path retries 17 times (about 23 s) and then says "not available". While this page waits for a preview,
  // "busy" is the expected answer (both share the service's one scan slot), so the layer path keeps retrying.
  // The clock is faked here, so the 30 s pass at once.
  const waitUnavailable = (x, n) => x.card.getByText('Layer path not available', { exact: true }).count().then(c => assert.equal(c, n))
  {
    const control = await start(1440, { clock: true })
    await control.set({ layerBusy: true })
    await control.live.click()
    await control.page.waitForFunction(() => window.__CC2_PREVIEW.layerCalls >= 1)
    await control.page.clock.runFor(30000)
    await control.card.getByText('Layer path not available', { exact: true }).first().waitFor()
    const calls = await control.page.evaluate(() => window.__CC2_PREVIEW.layerCalls)
    assert.ok(calls >= 17 && calls <= 19, `without a preview the layer path gives up after 17 retries: ${calls} requests`)
    await control.page.close()
  }
  {
    const waiting = await start(1440, { clock: true })
    await waiting.page.evaluate(() => {
      const s = window.__CC2_PREVIEW
      s.layerBusy = true
      s.previewGate = new Promise(r => (s.openGate = r))
    })
    await waiting.badge(E).click() // the preview request stays open
    await waiting.preview(E).locator('[data-object-preview-status="loading"]').waitFor()
    await waiting.live.click()
    assert.equal(await waiting.mapTab(), 'layer')
    await waiting.page.waitForFunction(() => window.__CC2_PREVIEW.layerCalls >= 1)
    await waiting.page.clock.runFor(30000)
    const calls = await waiting.page.evaluate(() => window.__CC2_PREVIEW.layerCalls)
    assert.ok(calls >= 21, `the layer path goes on retrying while the preview loads: ${calls} requests in 30 s`)
    await waitUnavailable(waiting, 0)
    assert.equal(await waiting.preview(E).locator('[data-object-preview-status]').getAttribute('data-object-preview-status'), 'loading')
    // The service is free again: the next retry brings the path, with no "not available" in between.
    await waiting.set({ layerBusy: false })
    await waiting.page.clock.runFor(2000)
    await waiting.page.waitForFunction(() => document.querySelectorAll('[data-layer-path]').length > 0)
    await waitUnavailable(waiting, 0)
    assert.match(await waiting.status.innerText(), /Layer 10 \/ 227/)
    // The preview arrives afterwards.
    await waiting.page.evaluate(() => {
      const s = window.__CC2_PREVIEW
      s.previewGate = null
      s.openGate()
    })
    await waiting.preview(E).locator('svg').waitFor()
    await waiting.page.close()
  }
  {
    // The wait ends with the preview: a deselected object no longer holds the layer path's retries open.
    const left = await start(1440, { clock: true })
    await left.page.evaluate(() => {
      const s = window.__CC2_PREVIEW
      s.layerBusy = true
      s.previewGate = new Promise(r => (s.openGate = r))
    })
    await left.badge(E).click()
    await left.preview(E).locator('[data-object-preview-status="loading"]').waitFor()
    await left.live.click()
    await left.page.waitForFunction(() => window.__CC2_PREVIEW.layerCalls >= 1)
    await left.badge(E).click() // deselect: the wait is over
    await left.anyPreview.first().waitFor({ state: 'detached' })
    await left.page.clock.runFor(30000)
    await left.card.getByText('Layer path not available', { exact: true }).first().waitFor()
    await left.page.close()
  }
  {
    // A preview that stays busy is given up after 17 retries, like the layer path: the field says it is not available.
    const giveUp = await start(1440, { clock: true })
    await giveUp.set({ previewBusy: true })
    await giveUp.badge(E).click()
    await giveUp.preview(E).locator('[data-object-preview-status="loading"]').waitFor()
    await giveUp.page.clock.runFor(30000)
    await giveUp.preview(E).locator('[data-object-preview-status="missing"]').waitFor()
    const asked = await giveUp.page.evaluate(() => window.__CC2_PREVIEW.previewStamps.length)
    assert.equal(asked, 18, `one request and 17 retries: ${asked}`)
    await giveUp.page.close()
  }

  // --- 6. Read hold (the printer homes, levels or starts a print): wait, ask every 3 s, never give up -------------
  {
    const hold = await start(1440, { clock: true })
    await hold.set({ previewHeld: true, layerHeld: true, previewStamps: [] })
    await hold.badge(E).click()
    await hold.preview(E).locator('[data-object-preview-status="held"]').waitFor()
    assert.match(await hold.preview(E).locator('[data-object-preview-status]').innerText(), /Waiting until the printer is ready/)
    await hold.live.click()
    await hold.page.waitForFunction(() => window.__CC2_PREVIEW.layerCalls >= 1)
    await hold.page.waitForFunction(() => /Waiting until the printer is ready/.test(document.querySelector('[data-layer-status]')?.textContent || ''))
    const calls0 = await hold.page.evaluate(() => window.__CC2_PREVIEW.layerCalls)
    await hold.page.clock.runFor(60000)
    const stamps = await hold.page.evaluate(() => [...window.__CC2_PREVIEW.previewStamps])
    const gaps = stamps.slice(1).map((t, i) => t - stamps[i])
    assert.ok(stamps.length >= 19 && stamps.length <= 22, `the held preview is asked again every 3 s: ${stamps.length} requests in 60 s`)
    assert.ok(gaps.every(gap => gap >= 3000), `never faster than every 3 s: ${gaps}`)
    const layerCalls = (await hold.page.evaluate(() => window.__CC2_PREVIEW.layerCalls)) - calls0
    assert.ok(layerCalls >= 18 && layerCalls <= 21, `the held layer path is asked again every 3 s: ${layerCalls} requests in 60 s`)
    // A minute of holding is neither "missing" nor "not available": both still wait with their text.
    assert.equal(await hold.preview(E).locator('[data-object-preview-status]').getAttribute('data-object-preview-status'), 'held')
    await waitUnavailable(hold, 0)
    assert.match(await hold.status.innerText(), /Waiting until the printer is ready/)
    // The hold ends: both come at the next request.
    await hold.set({ previewHeld: false, layerHeld: false })
    await hold.page.clock.runFor(3100)
    await hold.preview(E).locator('svg').waitFor()
    await hold.page.waitForFunction(() => document.querySelectorAll('[data-layer-path]').length > 0)
    assert.match(await hold.status.innerText(), /Layer 10 \/ 227/)
    await hold.page.close()
  }

  // --- Keyboard: Enter and Space select and deselect, on both tabs ------------------------------------------------
  {
    const k = await start()
    for (const tab of ['objects', 'layer']) {
      await k.showTab(tab)
      await k.badge(A).focus()
      for (const key of ['Enter', 'Space']) {
        await k.page.keyboard.press(key)
        await k.only(A)
        await k.preview(A).waitFor()
        await k.page.keyboard.press(key)
        await k.none()
        assert.equal(await k.page.evaluate(a => document.activeElement?.getAttribute('data-bed-object-id') === a, A), true, `${tab}/${key}: focus stays on the number`)
      }
      // The list works the same way.
      await k.listItem(C).focus()
      await k.page.keyboard.press('Enter')
      await k.only(C)
      await k.page.keyboard.press('Space')
      await k.none()
    }
    await k.page.close()
  }

  // --- Phone width: the map and the preview stay inside the page -------------------------------------------------
  {
    const longName = `${'Long_object_name_'.repeat(8)}.stl_id_9_copy_0`
    const narrow = await start(390)
    const fits = async what => {
      const wide = await narrow.page.evaluate(() => ({ page: document.documentElement.scrollWidth, view: innerWidth }))
      assert.ok(wide.page <= wide.view, `${what}: no sideways scroll (${wide.page} > ${wide.view})`)
      const box = await narrow.anyPreview.first().boundingBox()
      assert.ok(box.x >= 0 && box.x + box.width <= 390 + 0.5, `${what}: the preview field is inside the screen: ${JSON.stringify(box)}`)
      const inside = await narrow.card.evaluate(c => c.scrollWidth <= c.clientWidth)
      assert.ok(inside, `${what}: the card does not overflow`)
    }
    await narrow.set({
      objectRows: [
        { name: A, polygon: [[30, 30], [70, 30], [70, 70], [30, 70]] },
        { name: longName, center: [200, 200] },
        { name: C, center: [60, 200] },
      ],
    })
    await narrow.page.waitForFunction(() => document.querySelectorAll('[data-bed-object-id]').length === 3)
    for (const tab of ['objects', 'layer']) {
      await narrow.showTab(tab)
      await narrow.badge(A).click()
      const svg = narrow.preview(A).locator('svg')
      await svg.waitFor()
      await fits(`${tab} drawing`)
      const mapBox = await narrow.map.boundingBox()
      const box = await narrow.preview(A).boundingBox()
      assert.ok(box.y >= mapBox.y + mapBox.height - 1, `${tab}: below the map on a narrow screen`)
      const drawn = await svg.boundingBox()
      assert.ok(drawn.width > 100 && drawn.height > 100, `${tab}: the drawing has a size: ${JSON.stringify(drawn)}`)
      // The long object name wraps inside the field, the list row and the selection text.
      const sent = await narrow.page.evaluate(() => {
        window.__CC2_PREVIEW.previewGate = new Promise(() => {})
        return window.__CC2_PREVIEW.previewUrls.length
      })
      await narrow.listItem(longName).click()
      await narrow.preview(longName).locator('[data-object-preview-status="loading"]').waitFor()
      // The field shows "loading" before its request leaves (the request starts after the paint). Only open
      // the gate once that request waits behind it, or it would load the drawing and keep it in the cache.
      await narrow.page.waitForFunction(n => window.__CC2_PREVIEW.previewUrls.length === n + 1, sent)
      await fits(`${tab} loading with a long name`)
      // Every refusal text fits as well.
      await narrow.page.evaluate(() => (window.__CC2_PREVIEW.previewGate = null))
      for (const forced of [{ status: 413, error: 'Object preview file too large' }, { status: 413, error: 'Object preview scan limit reached' }, { status: 409, error: 'x' }, { status: 404, error: 'x' }]) {
        await narrow.page.evaluate(f => (window.__objectPreview = f), forced)
        await narrow.badge(longName).click() // deselect
        await narrow.anyPreview.first().waitFor({ state: 'detached' })
        await narrow.badge(longName).click()
        await narrow.preview(longName).locator('[data-object-preview-status]:not([data-object-preview-status="loading"])').waitFor()
        await fits(`${tab} refusal ${forced.status}`)
      }
      await narrow.page.evaluate(() => (window.__objectPreview = null))
      await narrow.badge(longName).click()
      await narrow.anyPreview.first().waitFor({ state: 'detached' })
    }
    await narrow.page.close()
  }

  console.log('PASS: Live from the Objects tab, select and deselect on both tabs by number, outline, list and keyboard, the same number look and status marks on both tabs, the object preview (loading, drawing, refusals, busy retries, one request per object, deselect), busy layer path while the preview waits, the read hold (own text, every 3 s, never given up), phone width')
} finally {
  await browser.close()
  await demo.cleanup()
}
