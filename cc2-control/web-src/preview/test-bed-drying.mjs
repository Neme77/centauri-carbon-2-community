import assert from 'node:assert/strict'
import { mkdir } from 'node:fs/promises'
import { chromium } from 'playwright'
import { createServer } from 'vite'
const server = await createServer({ root: new URL('..', import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'), mode: 'demo', server: { host: '127.0.0.1', port: 0 } })
let browser, language = 'en'
const choices = { schema: 1, editable: true, ready: true, canvas_uds: false, canvas_uds_available: false, quiet_motors: false, normal_confirmed: false, cli_canvas: false, bed_drying: false }
const session = { enabled: false, active: false, state: 'off', reason: 'none', fresh: true, bed: 25, target: 0, temperature: 70, minutes: 1, remaining: 0 }
const actions = [], errors = []
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  page.on('pageerror', e => errors.push(e.message))
  await page.route('**/api/preferences', r => r.fulfill({ json: { language, theme: 'dark' } }))
  await page.route('**/api/experiments', r => {
    if (r.request().method() === 'POST') {
      assert.equal(r.request().headers()['x-cc2-lan'], 'fixture-confirmation')
      choices.bed_drying = r.request().postData() === 'bed-drying:on'
      session.enabled = choices.bed_drying
    }
    return r.fulfill({ json: choices })
  })
  await page.route('**/api/printer', async r => {
    const response = await r.fetch(); const data = await response.json()
    data.connected = true; data.machine.status = 1; data.extruder = { temperature: 25, target: 0 }; data.heater_bed = { temperature: session.bed, target: session.target };
    data.bed_drying = { ...session }; data.experiments = { bed_drying: choices.bed_drying }
    return r.fulfill({ json: data })
  })
  await page.route('**/api/bed-drying/*', r => {
    assert.equal(r.request().headers()['x-cc2-lan'], 'fixture-confirmation')
    actions.push([r.request().url().split('/').pop(), r.request().postData()])
    if (actions.at(-1)[0] === 'start') Object.assign(session, { active: true, state: 'heating', target: 70 })
    else Object.assign(session, { state: 'stopping', reason: 'manual' })
    return r.fulfill({ json: session })
  })
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  assert.equal(await page.locator('[data-bed-drying]').count(), 0)
  await page.goto(`${server.resolvedUrls.local[0]}#settings/integrations`)
  const toggle = page.getByRole('switch', { name: 'Bed heating for filament drying', exact: true })
  await toggle.waitFor(); assert(await toggle.isDisabled())
  await page.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  await toggle.click(); await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => document.querySelectorAll('[data-experiments] [role=switch]')[2]?.getAttribute('aria-checked') === 'true')
  assert.deepEqual(actions, [], 'Enabling controls must not heat')
  await page.goto(`${server.resolvedUrls.local[0]}#control`)
  const card = page.locator('[data-bed-drying]'); await card.waitFor()
  const start = card.getByRole('button', { name: 'Start bed heating', exact: true })
  assert(await start.isDisabled())
  await card.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  await card.getByLabel('Duration, minutes', { exact: true }).fill('0'); assert(await start.isDisabled())
  await card.getByLabel('Duration, minutes', { exact: true }).fill('1')
  await start.click(); await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click(); assert.deepEqual(actions, [])
  await start.click(); await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await card.getByText('Heating', { exact: true }).waitFor(); assert.deepEqual(actions, [['start', '70:1']])
  assert(await start.isDisabled()); assert(await card.getByLabel('Duration, minutes', { exact: true }).isDisabled())
  Object.assign(session, { state: 'holding', bed: 70, remaining: 55 })
  await card.getByText('Holding temperature', { exact: true }).waitFor()
  await page.reload(); await card.getByText('Holding temperature', { exact: true }).waitFor(); assert.equal(actions.length, 1)
  await card.getByLabel('LAN code for experimental features', { exact: true }).fill('fixture-confirmation')
  await card.getByRole('button', { name: 'Stop bed heating', exact: true }).click()
  await card.getByText('Stopping · awaiting confirmation', { exact: true }).waitFor(); assert(session.active)
  const stoppedText = 'Bed drying stopped. Bed target confirmed at 0 °C.'
  const warningText = 'Session interrupted or changed elsewhere. Check the bed target; stopping is confirmed only after zero readback.'
  assert.equal(await card.getByText(stoppedText, { exact: true }).count(), 0, 'A stop request is not zero confirmation')
  for (const pending of [
    { active: false, state: 'interrupted', target: 70, fresh: true, reason: 'manual' },
    { active: false, state: 'interrupted', target: 0, fresh: false, reason: 'manual' },
    { active: false, state: 'interrupted', target: 0, fresh: true, reason: 'changed' },
  ]) {
    Object.assign(session, pending)
    await page.reload(); await card.getByText(warningText, { exact: true }).waitFor()
    assert.equal(await card.getByText(stoppedText, { exact: true }).count(), 0, 'Stale/nonzero or externally changed targets must not confirm a manual stop')
  }
  Object.assign(session, { active: false, state: 'interrupted', target: 0, fresh: true, reason: 'manual' })
  await page.reload(); await card.getByText(stoppedText, { exact: true }).waitFor()
  await card.getByText('Interrupted', { exact: true }).waitFor()
  assert.equal(await card.getByText(warningText, { exact: true }).count(), 0)

  await page.setViewportSize({ width: 390, height: 844 }); await card.scrollIntoViewIfNeeded()
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1))
  await mkdir('preview/artifacts', { recursive: true })
  await card.screenshot({ path: 'preview/artifacts/bed-drying-mobile.png' })
  await page.setViewportSize({ width: 1440, height: 1000 }); await card.screenshot({ path: 'preview/artifacts/bed-drying-desktop.png' })
  // The enabled drying card has two controls; footers align within each row of the two-column grid.
  for (const locale of ['en', 'it']) for (const enabled of [false, true]) {
    language = locale; choices.bed_drying = enabled; session.enabled = enabled
    await page.goto(`${server.resolvedUrls.local[0]}#settings/integrations`); await page.reload()
    await page.waitForFunction(lang => document.documentElement.lang === lang, locale)
    await page.waitForFunction(expected => document.querySelectorAll('[data-experiments] [role=switch]')[2]?.getAttribute('aria-checked') === String(expected), enabled)
    for (const width of [1440, 768, 390]) {
      await page.setViewportSize({ width, height: 1100 })
      // Container queries settle on the next rendering frame after a viewport resize.
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))))
      const geometry = await page.locator('[data-experiment-actions]').evaluateAll(rows => rows.map(row => ({
        row: row.getBoundingClientRect().toJSON(),
        buttons: [...row.querySelectorAll('button')].map(b => b.getBoundingClientRect().toJSON()),
      })))
      assert.equal(geometry.length, 4)
      if (width === 1440) {
        for (let i = 0; i < geometry.length; i += 2) {
          const tops = geometry.slice(i, i + 2).map(g => g.buttons[0].top)
          assert(Math.max(...tops) - Math.min(...tops) <= 1, `${locale}: desktop controls must align in row ${i / 2} with drying ${enabled}`)
        }
      }
      if (enabled) {
        const [toggle, control] = geometry[2].buttons
        assert.equal(geometry[2].buttons.length, 2)
        assert(Math.abs(toggle.top - control.top) <= 1, `${locale}: drying controls must share a row at ${width}: ${JSON.stringify(geometry[2])}`)
        assert(control.left >= toggle.right + 6, `${locale}: drying controls must have a gap`)
      }
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `${locale}: overflow at ${width}`)
      if (locale === 'it' && enabled && width !== 768) {
        await page.locator('[data-experiments]').screenshot({ path: `preview/artifacts/experiment-actions-${width}.png` })
      }
    }
  }
  assert.deepEqual(errors, []); assert.equal(actions.length, 2)
  console.log('PASS opt-in, no heat on enable, validation, actual start/cancel/stop controls, reload continuity, pending zero confirmation and mobile layout')
} finally { await browser?.close(); await server.close() }
