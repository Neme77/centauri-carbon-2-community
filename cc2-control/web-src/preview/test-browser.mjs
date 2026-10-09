import assert from 'node:assert/strict'
import { mkdir, readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
// An accidental proxy would fail the API and browser checks below.
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } })
  const errors = [], outside = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (!r.url().startsWith(`${origin}/`) && !r.url().startsWith('blob:') && !r.url().startsWith('data:')) outside.push(r.url())
  })
  await page.goto(origin)
  await page.locator('#cc2-preview-banner').waitFor()
  await page.waitForFunction(() => document.querySelector('main')?.textContent.includes('143'))
  assert.equal((await page.request.get(`${origin}/api/printer`)).status(), 200)
  assert.equal((await page.request.get(`${origin}/api/unknown`)).status(), 404)
  assert.equal((await page.request.post(`${origin}/api/control`, { data: 'system:emergency_stop' })).status(), 403)
  assert.equal((await page.request.post(`${origin}/api/gcode-files/upload?name=demo.gcode`, { data: 'G28' })).status(), 403)
  assert.equal((await page.request.post(`${origin}/api/console/command`, { data: 'G28' })).status(), 403)
  assert.equal((await page.request.post(`${origin}/api/setup`, { data: '123456' })).status(), 403)
  const naturalControl = async () => {
    const layout = await page.locator('.cc2-control-columns').evaluate(e => {
      const groups = [...e.children]
      if (getComputedStyle(groups[1]).display === 'contents') {
        const temperatures = groups[1].children[0].getBoundingClientRect()
        const fans = groups[1].children[1].getBoundingClientRect()
        const machine = groups[2].children[0].getBoundingClientRect()
        const offset = groups[2].children[1].getBoundingClientRect()
        return { grid: true, rowGap: parseFloat(getComputedStyle(e).rowGap), gaps: [fans.top - temperatures.bottom, offset.top - machine.bottom], aligned: Math.abs(fans.top - offset.top) < 2 }
      }
      return { grid: false, rowGap: 0, gaps: groups.flatMap(g => {
        const cards = [...g.children].map(c => c.getBoundingClientRect())
        return cards.slice(1).map((c, i) => c.top - cards[i].bottom)
      }), aligned: true }
    })
    assert.ok(layout.gaps.every(d => d >= 8) && (layout.grid ? layout.rowGap >= 8 && layout.rowGap <= 16 : layout.gaps.every(d => d <= 16)), 'Control rows retain compact gaps')
    assert.ok(layout.aligned, 'Fans align with live Z offset')
  }
  const shots = process.env.CC2_SCREENSHOTS
  if (shots) await mkdir(shots, { recursive: true })
  for (const [width, height] of [[1440, 900], [1366, 640], [1280, 720], [1920, 1080], [1024, 768], [768, 1024], [390, 844], [740, 390], [1280, 480]]) {
    await page.setViewportSize({ width, height })
    const rail = page.locator('#cc2-navigation button[aria-expanded]')
    if (await rail.isVisible() && await rail.getAttribute('aria-expanded') !== 'true') { await rail.click(); await page.waitForTimeout(200) }
    for (const tab of ['dashboard', 'control', 'job', 'files', 'history', 'bed', 'canvas', 'spools', 'console', 'settings']) {
      if (await page.locator('#cc2-menu-toggle').isVisible()) await page.locator('#cc2-menu-toggle').click()
      await page.locator(`a[href="#${tab}"]`).click()
      await page.waitForTimeout(150)
      if (tab === 'job') assert.equal(await page.locator('main .cc2-tuning').count(), 0, 'Job must not duplicate dashboard tuning')
      assert.ok(await page.locator('main').innerText(), `${width} ${tab}: empty page`)
      const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1)
      assert.equal(overflow, false, `${width} ${tab}: horizontal overflow`)
      if (tab === 'control' && width >= 1280 && height > 500) await naturalControl()
      if (shots) await page.screenshot({ path: path.join(shots, `${width}-${tab}.png`), fullPage: true })
    }
  }
  for (const [width, height] of [[1366, 640], [1280, 720], [1920, 1080], [1024, 768]]) {
    await page.setViewportSize({ width, height })
    const rail = page.locator('#cc2-navigation button[aria-expanded]')
    if (await rail.getAttribute('aria-expanded') !== 'true') await rail.click()
    await page.waitForTimeout(200)
    await rail.click()
    await page.waitForTimeout(200)
    assert.equal(await page.locator('header').evaluate(e => Math.round(e.getBoundingClientRect().height)), 60)
    for (const tab of ['dashboard', 'control', 'job', 'files', 'history', 'bed', 'canvas', 'spools', 'console', 'settings']) {
      await page.locator(`a[href="#${tab}"]`).click()
      await page.waitForTimeout(150)
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, `${width} collapsed ${tab}: overflow`)
      if (tab === 'control' && width >= 1280) await naturalControl()
      if (tab === 'dashboard') assert.ok(await page.locator('.cc2-camera-frame').evaluate(e => e.getBoundingClientRect().height >= 180))
      if (shots) await page.screenshot({ path: path.join(shots, `${width}-collapsed-${tab}.png`), fullPage: true })
    }
  }
  await page.setViewportSize({ width: 1440, height: 900 })
  const rail = page.locator('#cc2-navigation button[aria-expanded]')
  if (await rail.getAttribute('aria-expanded') !== 'true') await rail.click()
  await page.waitForTimeout(200)
  await page.locator('#cc2-navigation button[aria-expanded]').click()
  for (const tab of ['dashboard', 'control', 'job', 'files', 'history', 'bed', 'canvas', 'spools', 'console', 'settings']) {
    await page.locator(`a[href="#${tab}"]`).click()
    await page.waitForTimeout(200)
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1), false, `Collapsed sidebar: ${tab} overflow`)
    if (shots) await page.screenshot({ path: path.join(shots, `1440-collapsed-${tab}.png`), fullPage: true })
  }
  await page.getByRole('tab').nth(2).click()
  const languages = page.locator('main select:has(option[value="zh"])')
  for (const lang of ['en', 'fr', 'zh', 'ru', 'it']) {
    await languages.selectOption(lang)
    await page.waitForFunction(lang => document.documentElement.lang === lang, lang)
  }
  const themes = page.locator('main select:has(option[value="light"])')
  for (const theme of ['light', 'dark']) {
    await themes.selectOption(theme)
    await page.waitForFunction(theme => document.documentElement.dataset.theme === theme, theme)
  }
  await page.locator('a[href="#dashboard"]').click()
  // Tuning reset exercises the same form action as the real UI, but only modifies fixtures.
  const response = page.waitForResponse(r => r.url().endsWith('/api/control') && r.request().method() === 'POST')
  await page.locator('#tune-speed').locator('..').getByRole('button').last().click()
  assert.equal((await response).status(), 200)
  for (const [scenario, expected] of [['paused', 'paused'], ['idle', ''], ['disconnected', ''], ['printing', 'printing']]) {
    await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption(scenario)])
    await page.locator('#cc2-preview-banner').waitFor()
    const data = await (await page.request.get(`${origin}/api/printer`)).json()
    assert.equal(data.print.state, expected)
    assert.equal(data.connected, scenario !== 'disconnected')
  }
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-reset').click()])
  assert.equal((await (await page.request.get(`${origin}/__preview/scenario`)).json()).scene, 'printing')
  const labels = JSON.parse(await readFile(path.join(root, 'public/locales/it.json'), 'utf8'))
  // Print history: rendering needs an idle printer, a rendered video becomes a download.
  const refreshed = page.waitForResponse(r => r.url().endsWith('/api/history/refresh') && r.request().method() === 'POST')
  await page.locator('#cc2-navigation a[href="#history"]').click()
  assert.equal((await refreshed).status(), 202, 'Opening History asks the printer once')
  await page.getByText('CC2_Preview_Vase_PETG.gcode', { exact: true }).waitFor()
  const create = page.getByRole('button', { name: labels['history.create_video'], exact: true })
  const downloads = page.locator('main button', { hasText: labels['history.download_video'] })
  assert.equal(await create.count(), 2)
  assert.ok(await create.first().isDisabled(), 'Rendering a video requires an idle printer')
  assert.equal(await downloads.count(), 1)
  await page.getByText(labels['history.previous_attempt_failed'], { exact: true }).waitFor()
  // Canvas auto refill follows the printer's readback.
  await page.locator('#cc2-navigation a[href="#canvas"]').click()
  const refill = page.locator('main button[aria-pressed]')
  await refill.waitFor()
  await page.waitForFunction(() => document.querySelector('main button[aria-pressed]')?.textContent.includes(' '))
  assert.equal(await refill.getAttribute('aria-pressed'), 'false')
  const toggled = page.waitForResponse(r => r.url().endsWith('/api/canvas/auto-refill'))
  await refill.click()
  assert.equal((await toggled).request().postData(), 'on')
  await page.waitForFunction(() => document.querySelector('main button[aria-pressed]')?.getAttribute('aria-pressed') === 'true')
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('idle')])
  await page.locator('#cc2-navigation a[href="#history"]').click()
  await create.first().waitFor()
  assert.ok(await create.first().isEnabled())
  await create.first().click()
  await page.getByRole('dialog').getByRole('button', { name: labels['common.confirm'], exact: true }).click()
  await downloads.nth(1).waitFor()
  // A printer refusal reported by /api/printer is shown once, with the vendor meaning of its code.
  let reportRefusal = false
  await page.route('**/api/printer', async route => {
    const response = await route.fetch()
    const data = await response.json()
    if (reportRefusal) data.printer_error = { sequence: 7, method: 1020, code: 1026, age: 0 }
    await route.fulfill({ response, json: data })
  })
  await page.reload()
  // Preferences and the Italian dictionary load asynchronously after mounting.
  // Report a new refusal only once the localized UI is ready, as during normal use.
  await create.first().waitFor()
  reportRefusal = true
  await page.getByRole('alert').filter({ hasText: labels['printer.bed_mesh_missing'] }).waitFor()
  await page.unroute('**/api/printer')
  await Promise.all([page.waitForEvent('load'), page.locator('#preview-scene').selectOption('paused')])
  // Sub-state: a paused job is still machine state "Printing"; the vendor sub-state says "Paused".
  await page.locator('#cc2-navigation a[href="#dashboard"]').click()
  await page.getByText(`${labels['state.printing']} · ${labels['substate.paused']}`, { exact: true }).first().waitFor()
  await page.locator('#cc2-navigation a[href="#control"]').click()
  assert.ok(await page.getByRole('button', { name: labels['common.motors_off'], exact: true }).isDisabled())
  assert.ok(await page.getByRole('button', { name: labels['common.fans_off'], exact: true }).isDisabled())
  let offsetPending = true
  await page.route('**/api/printer', async route => {
    const response = await route.fetch()
    const data = await response.json()
    data.z_offset = { value: 0.06, reference: 0.06, pending: offsetPending, timed_out: !offsetPending }
    await route.fulfill({ response, json: data })
  })
  await page.reload()
  await page.getByRole('button', { name: '+ 0.01', exact: true }).waitFor()
  assert.ok(await page.getByRole('button', { name: '+ 0.01', exact: true }).isDisabled())
  offsetPending = false
  await page.reload()
  await page.getByText(labels['control.z_offset_readback_timeout'], { exact: true }).waitFor()
  assert.ok(await page.getByRole('button', { name: '+ 0.01', exact: true }).isEnabled())
  await page.locator('#cc2-navigation a[href="#settings"]').click()
  await page.getByRole('tab').nth(2).click()
  await page.locator('main select:has(option[value="light"])').selectOption('light')
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'light')
  await page.route('**/api/preferences', async route => {
    if (route.request().method() === 'PUT' && route.request().postDataJSON()?.quick1) {
      return route.fulfill({ status: 500, json: { error: 'Simulated quick-action save failure' } })
    }
    await route.continue()
  })
  await page.getByRole('button', { name: labels['settings.restore_defaults'], exact: true }).click()
  await page.getByRole('dialog').getByRole('button', { name: labels['common.confirm'], exact: true }).click()
  await page.getByRole('alert').filter({ hasText: 'Simulated quick-action save failure' }).waitFor()
  assert.equal(await page.evaluate(() => document.documentElement.dataset.theme), 'light')
  assert.equal(await page.evaluate(() => document.documentElement.lang), 'it')
  assert.deepEqual(errors, [])
  assert.deepEqual(outside, [], 'Preview must never request printer ports or external services')
  console.log('PASS: isolated preview, all pages at 9 viewport sizes and desktop sidebar states, scenario controls, tuning reset, history, auto refill, printer refusals, sub-states, blocked hardware actions and local-only requests')
} finally {
  await browser?.close()
  await server.close()
}
