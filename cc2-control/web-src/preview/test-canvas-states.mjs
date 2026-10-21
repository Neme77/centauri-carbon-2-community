import assert from 'node:assert/strict'
import { chromium } from 'playwright'
import { createServer } from 'vite'
import { fixture, launchOptions, root } from './fixture.mjs'

// Deterministic limits and discontinuities supplement the actual UI path below.
const logicServer = await createServer({ root, configFile: false, server: { middlewareMode: true } })
try {
  const { observeCanvasSlots: observe, observedPrinterPhase: phase, canvasTimeline: history, clearCanvasTimeline: clear } =
    await logicServer.ssrLoadModule('/src/lib/canvas-events.ts')
  const state = value => [{ slot: 0, state: value }]
  const reading = (status, sub_status = 0) => ({
    ok: true, at: 1000, data: { connected: true, machine: { status, sub_status } },
  })
  assert.equal(phase(reading(1), 1500), 'idle')
  assert.equal(phase(reading(2), 1500), 'printing')
  assert.equal(phase(reading(2, 2502), 1500), 'paused')
  assert.equal(phase(reading(2, 2505), 1500), 'paused')
  assert.equal(phase(reading(13), 1500), 'unknown', 'filament operation is not printing')
  assert.equal(phase(reading(2), 6001), 'unknown', 'stale state must be unknown')
  assert.equal(phase(reading(2), 999), 'unknown', 'future readback must be unknown')
  assert.equal(phase({ ...reading(2), ok: false }, 1500), 'unknown')
  assert.equal(phase({ ...reading(2), data: { connected: false, machine: { status: 2 } } }, 1500), 'unknown')
  assert.equal(phase(null, 1500), 'unknown')
  observe(true, state('empty'), 1)
  assert.equal(history.get().events.length, 0, 'first read only sets the baseline')
  for (let i = 0; i < 40; i++) observe(true, state(i % 2 ? 'empty' : 'present'), i + 2)
  assert.equal(history.get().events.length, 24, 'more than 24 actual changes evict oldest events')
  assert.equal(history.get().events[0].time, 41)
  // Phase annotations are fixed enums; no filenames, material names or secrets.
  observe(true, state('present'), 42, 'printing')
  assert.equal(history.get().events[0].phase, 'printing')
  assert.equal(history.get().events.length, 24, 'phase annotation retains the existing history bound')
  clear()
  observe(false, [], 50)
  observe(true, state('loaded'), 51)
  assert.equal(history.get().events.length, 0, 'reconnection establishes a fresh baseline')
  observe(true, [{ slot: 0, state: 'loaded' }, { slot: 0, state: 'empty' }], 52)
  observe(true, state('present'), 53)
  assert.equal(history.get().events.length, 0, 'duplicate slot rows invalidate comparison')
  observe(true, state('unknown'), 54)
  observe(true, state('empty'), 55)
  assert.equal(history.get().events.length, 0, 'unknown states invalidate comparison')
} finally {
  await logicServer.close()
}

const demo = await fixture()
const browser = await chromium.launch(launchOptions)
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  await page.addInitScript(() => localStorage.setItem('cc2-fixture-preferences', '{"language":"en"}'))
  await page.goto(demo.url + '#canvas')
  const load = page.locator('[data-canvas-action="canvas.load"]')
  const unload = page.locator('[data-canvas-action="canvas.unload"]')
  const card = slot => page.locator(`[data-canvas-slot="${slot}"]`)
  await load.waitFor()
  assert.equal(await load.isDisabled(), true)
  assert.equal(await unload.isDisabled(), true)
  await card(0).getByText('Empty', { exact: true }).waitFor()
  await card(1).getByRole('button', { name: 'Select slot', exact: true }).click()
  assert.equal(await load.isDisabled(), false)
  assert.equal(await unload.isDisabled(), true)
  await card(1).getByText('Present · unloaded from head', { exact: true }).waitFor()
  await page.evaluate(() => {
    Object.assign(window.__CC2_PREVIEW, { activeTray: 1, headLoaded: true })
    window.__CC2_PREVIEW.trays[1].status = 2
  })
  await card(1).getByText('Loaded to head', { exact: true }).waitFor()
  const timeline = page.locator('[data-canvas-timeline]')
  await timeline.locator('[data-canvas-event]').filter({ hasText: 'Present · unloaded from head → Loaded to head' }).waitFor()
  assert.ok((await timeline.locator('[data-canvas-event]').count()) <= 24, 'Timeline is bounded')
  await timeline.locator('[data-canvas-printer-phase]').first().waitFor()
  assert.match(await timeline.locator('[data-canvas-printer-phase]').first().textContent(), /Printer \(recent status\): (Idle|Printing|Paused|Unknown)/)
  assert.equal(await load.isDisabled(), true)
  assert.equal(await unload.isDisabled(), false)
  await card(2).getByRole('button', { name: 'Select slot', exact: true }).click()
  assert.equal(await load.isDisabled(), true, 'another loaded channel prevents loading')
  assert.equal(await unload.isDisabled(), true, 'only the loaded channel can unload')
  // Captured reboot case: head sensor true, active_cid -1, all slots present.
  // The dedicated recovery action never changes ordinary Load/Unload policy.
  await page.evaluate(() => {
    window.__CC2_PREVIEW.activeTray = -1
    window.__CC2_PREVIEW.trays.forEach(t => { t.status = 1 })
  })
  const recovery = page.locator('[data-canvas-recovery]')
  await recovery.getByText('Canvas state needs recovery', { exact: true }).waitFor()
  assert.match(await recovery.textContent(), /any present slot/)
  assert.match(await recovery.textContent(), /external spool/)
  assert.equal(await load.isDisabled(), true)
  assert.equal(await unload.isDisabled(), true)
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [])
  await page.reload()
  await page.locator('[data-canvas-slot="1"]').getByRole('button', { name: 'Select slot', exact: true }).click()
  await page.evaluate(() => { window.__CC2_PREVIEW.headLoaded = true; window.__CC2_PREVIEW.activeTray = -1 })
  await recovery.waitFor()
  assert.equal(await load.isDisabled(), true, 'reloading or selecting a card cannot acknowledge recovery')
  await page.evaluate(() => { window.__CC2_PREVIEW.status = 2 })
  await recovery.waitFor({ state: 'hidden' })
  assert.equal(await load.isDisabled(), true, 'printing never offers recovery loading')
  await page.evaluate(() => { window.__CC2_PREVIEW.status = 1; window.__CC2_PREVIEW.canvasFresh = false })
  await card(1).getByText('State unavailable', { exact: true }).waitFor()
  assert.equal(await recovery.isVisible(), false, 'stale state does not offer native recovery instructions')
  await page.evaluate(() => {
    Object.assign(window.__CC2_PREVIEW, { canvasFresh: true, activeTray: 1 })
    window.__CC2_PREVIEW.trays[1].status = 2
  })
  await card(1).getByText('Loaded to head', { exact: true }).waitFor()
  await recovery.waitFor({ state: 'hidden' })
  assert.equal(await unload.isDisabled(), false, 'native loaded-state confirmation restores normal actions')
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [])
  await card(2).getByRole('button', { name: 'Select slot', exact: true }).click()
  await page.evaluate(() => { window.__CC2_PREVIEW.headLoaded = false })
  await card(2).getByText('Transition · waiting for confirmation', { exact: true }).waitFor()
  assert.equal(await load.isDisabled(), true)
  await page.evaluate(() => { window.__CC2_PREVIEW.activeTray = -1 })
  await page.waitForFunction(() => !document.querySelector('[data-canvas-action="canvas.load"]').disabled)
  await page.evaluate(() => { window.__CC2_PREVIEW.status = 2 })
  await page.waitForFunction(() => document.querySelector('[data-canvas-action="canvas.load"]').disabled)
  await page.evaluate(() => { window.__CC2_PREVIEW.status = 1; window.__CC2_PREVIEW.canvasFresh = false })
  await card(2).getByText('State unavailable', { exact: true }).waitFor()
  assert.equal(await load.isDisabled(), true)
  assert.equal(await unload.isDisabled(), true)
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [], 'state changes and disabled actions send no commands')
  await timeline.getByRole('button', { name: 'Clear history', exact: true }).click()
  await timeline.locator('[data-canvas-timeline-empty]').waitFor()
  assert.equal(await timeline.locator('[data-canvas-event]').count(), 0, 'History clears without printer commands')
  assert.deepEqual(await page.evaluate(() => window.__CC2_PREVIEW.commands), [], 'Clear must not send a hardware command')
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    await page.waitForTimeout(200)
    const overflow = await page.evaluate(() => ({ width: innerWidth, scroll: document.documentElement.scrollWidth,
      elements: [...document.querySelectorAll('main *')].filter(e => e.getBoundingClientRect().right > innerWidth + 1).slice(0, 8).map(e => [e.tagName, e.className, e.textContent.slice(0, 70)]) }))
    assert(overflow.scroll <= overflow.width, JSON.stringify(overflow))
  }
  console.log('PASS: Canvas slot state and action eligibility across empty, parked, loaded, other channel, transition, print and unavailable state')
} finally {
  await browser.close()
  await demo.cleanup()
}
