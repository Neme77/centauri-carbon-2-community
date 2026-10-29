import assert from 'node:assert/strict'
import { readFile, mkdir, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { createRequire } from 'node:module'

const app = path.resolve(process.argv[2])
const output = path.resolve(process.argv[3] || 'output/network-ui')
const require = createRequire(path.join(app, 'web-src/package.json'))
const { chromium } = require('playwright')
const html = await readFile(path.join(app, 'web/index.html'), 'utf8')
await mkdir(output, { recursive: true })
const dictionary = async language => JSON.parse(await readFile(path.join(app, `web-src/public/locales/${language}.json`), 'utf8'))
let prefs = { language: 'en', theme: 'obsidian' }
let replyCode = 200, calls = 0
let cacheTimeline = null
const cacheSamples = []
const mutations = [], errors = [], outside = []
const iface = { present: true, enabled: true, carrier: true, address: '192.0.2.103', address_mode: 'unknown', link_mbps: null }
let snapshot = {
  schema: 1,
  wifi: { ...iface },
  ethernet: { ...iface, present: false, enabled: false, carrier: null, address: null },
  wireless: { control_available: true, state: 'COMPLETED', ssid: 'Home network', security: 'WPA2-PSK', frequency_mhz: 5220, signal_dbm: -47 },
  route: { selected: 'wifi' },
}
const browser = await chromium.launch({ headless: true,
  ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu'] } : {}),
})
try {
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } })
  await context.route('**/*', async route => {
    const url = new URL(route.request().url())
    const headers = { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': '*', 'Access-Control-Allow-Methods': '*' }
    if (url.hostname !== 'cc2-network.test') { outside.push(url.href); return route.abort() }
    if (route.request().method() === 'OPTIONS') return route.fulfill({ status: 204, headers })
    if (url.pathname === '/') return route.fulfill({ body: html, contentType: 'text/html', headers })
    if (url.pathname.startsWith('/i18n/')) return route.fulfill({ json: await dictionary(path.basename(url.pathname, '.json')), headers })
    if (!['GET', 'HEAD'].includes(route.request().method())) mutations.push(url.pathname)
    if (url.pathname === '/api/network') {
      calls++
      let envelope = { available: true, fresh: true, refreshing: false, age_ms: 0, snapshot }
      if (cacheTimeline) {
        const now = await page.evaluate(() => performance.now())
        if (cacheTimeline.readyAt !== null && now >= cacheTimeline.readyAt) {
          cacheTimeline.sampleAt = cacheTimeline.readyAt
          cacheTimeline.readyAt = null
        }
        if (cacheTimeline.readyAt === null && now - cacheTimeline.attemptedAt >= 3000) {
          cacheTimeline.attemptedAt = now
          cacheTimeline.readyAt = now + 600
        }
        const age = now - cacheTimeline.sampleAt
        envelope = { available: true, fresh: age < 8000, refreshing: cacheTimeline.readyAt !== null, age_ms: age, snapshot }
        cacheSamples.push({ at: now, age, refreshing: envelope.refreshing })
      }
      return route.fulfill({ status: replyCode, json: replyCode === 200 ? envelope : { error: 'fixture unavailable' }, headers })
    }
    const routes = {
      '/api/preferences': prefs,
      '/api/health': { version: '1.2.23', mqtt_connected: true, mqtt_registered: true, snapshot_received: true },
      '/api/setup': { configured: true, snapshot_received: true, mqtt_connected: true },
      '/api/printer': { connected: true, last_message_age: 0, machine: { status: 1, status_name: 'Idle' },
        extruder: { temperature: 25, target: 0 }, heater_bed: { temperature: 23, target: 0 },
        print: { enabled: false, state: 'standby' }, spools: { enabled: false, questions: [] },
        experiments: { quiet_motors: false, canvas_uds: false, bed_drying: false }, recovery: { available: false } },
      '/api/spools': { available: false },
    }
    return route.fulfill({ json: routes[url.pathname] ?? {}, headers })
  })
  const page = await context.newPage()
  page.on('pageerror', e => errors.push(e.message))
  const navigate = async () => {
    if (page.url().startsWith('http://cc2-network.test/')) await page.reload()
    else await page.goto('http://cc2-network.test/#settings/network')
    await page.locator('.cc2-network').waitFor()
    await page.waitForFunction(() => document.querySelector('.cc2-network')?.textContent.includes('192.0.2.103'))
    await page.waitForTimeout(220)
  }
  await navigate()
  const en = await dictionary('en')
  const toggle = page.getByRole('switch', { name: en['network.show_options'] })
  assert.equal(await toggle.getAttribute('aria-checked'), 'false')
  assert.equal(await page.locator('.cc2-network').getByRole('heading', { name: 'Ethernet', exact: true }).count(), 0)
  const y1 = (await toggle.boundingBox()).y
  await toggle.click()
  assert.equal(await toggle.getAttribute('aria-checked'), 'true')
  assert(Math.abs((await toggle.boundingBox()).y - y1) < 1, 'toggle moved when cards appeared')
  await page.getByText(en['network.missing_interface'], { exact: true }).waitFor()
  assert.equal(await page.getByRole('radio', { name: en['network.wifi'], exact: true }).getAttribute('aria-checked'), 'true')
  assert.equal(await page.getByRole('button', { name: en['network.prepare_trial'], exact: true }).isDisabled(), true)
  assert.equal(await page.locator('.cc2-network [role="radio"]:not([disabled])').count(), 0)
  assert.equal(await page.getByRole('radio', { name: en['network.automatic'], exact: true }).count(), 0)
  await page.reload()
  await page.locator('.cc2-network').waitFor()
  assert.equal(await toggle.getAttribute('aria-checked'), 'true', 'visibility preference not retained')
  await page.getByText('192.0.2.103', { exact: true }).waitFor()
  await page.screenshot({ path: path.join(output, 'desktop.png'), fullPage: true })
  snapshot = { ...snapshot, wireless: { ...snapshot.wireless, ssid: '<img src=x onerror="window.bad=1">', signal_dbm: null } }
  await page.getByRole('button', { name: en['network.refresh'], exact: true }).click()
  await page.getByRole('heading', { name: snapshot.wireless.ssid, exact: true }).waitFor()
  assert.equal(await page.locator('.cc2-network img').count(), 0)
  assert.equal(await page.evaluate(() => window.bad), undefined)
  snapshot = { ...snapshot, wireless: { ...snapshot.wireless, ssid: 'Home network', signal_dbm: -47 },
    ethernet: { ...iface, address: '192.0.2.107', link_mbps: 1000 }, route: { selected: 'ethernet' } }
  await page.getByRole('button', { name: en['network.refresh'], exact: true }).click()
  await page.getByText('192.0.2.107', { exact: true }).waitFor()
  assert.equal(await page.getByRole('radio', { name: en['network.ethernet'], exact: true }).getAttribute('aria-checked'), 'true')
  replyCode = 503
  await page.getByRole('button', { name: en['network.refresh'], exact: true }).click()
  await page.locator('.cc2-network [role="status"]').waitFor()
  assert.equal(await page.getByText('192.0.2.107', { exact: true }).count(), 0, 'failed read kept apparently live IP')
  replyCode = 200
  await page.getByRole('button', { name: en['network.refresh'], exact: true }).click()
  await page.getByText('192.0.2.107', { exact: true }).waitFor()
  for (const language of ['en', 'it', 'fr', 'es', 'pl', 'cs', 'hu', 'ru', 'zh']) {
    prefs.language = language
    await navigate()
    const dict = await dictionary(language)
    await page.getByText(dict['network.readonly'], { exact: true }).waitFor()
    for (const width of [320, 390, 768, 1440]) {
      await page.setViewportSize({ width, height: 1000 })
      await page.waitForTimeout(220) // main/sidebar margin transition after viewport changes
      if (!(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))) {
        console.log(await page.evaluate(() => [...document.querySelectorAll('.cc2-network *')].map(el => ({ tag: el.tagName, text: el.textContent.slice(0,50), left: el.getBoundingClientRect().left, right: el.getBoundingClientRect().right, width: el.clientWidth, scroll: el.scrollWidth })).filter(el => el.right > innerWidth + 1 || el.scroll > el.width + 2)))
        await page.screenshot({ path: path.join(output, 'overflow.png'), fullPage: true })
        throw new Error(`${language} at ${width}: page overflow`)
      }
      const overflow = await page.locator('.cc2-network button').evaluateAll(buttons => buttons.some(b => b.scrollWidth > b.clientWidth + 1))
      assert(!overflow, `${language} at ${width}: button overflow`)
    }
  }
  prefs.language = 'it'
  for (const theme of ['obsidian', 'dark', 'light']) {
    prefs.theme = theme
    await navigate()
    await page.waitForFunction(value => document.documentElement.dataset.theme === value, theme)
    if (process.env.CC2_AXE_PATH) {
      await page.addScriptTag({ path: process.env.CC2_AXE_PATH })
      const violations = await page.evaluate(async () => (await window.axe.run(document, { runOnly: { type: 'tag', values: ['wcag2a', 'wcag2aa', 'wcag21aa'] } })).violations.map(v => ({ id: v.id, nodes: v.nodes.map(n => n.target) })))
      assert.deepEqual(violations, [], `accessibility: ${theme}`)
    }

  }
  prefs.theme = 'obsidian'
  snapshot.route.selected = 'wifi'
  await navigate()
  await page.setViewportSize({ width: 1440, height: 1000 })
  await page.waitForTimeout(220)
  await page.screenshot({ path: path.join(output, 'desktop.png'), fullPage: true })
  await page.setViewportSize({ width: 390, height: 1000 })
  await page.waitForTimeout(220)
  await page.screenshot({ path: path.join(output, 'mobile.png'), fullPage: true })
  const it = await dictionary('it')
  // Reproduce the real provider: a response starts an async refresh but still
  // contains its previous cached sample. Static zero-age mocks missed R1.
  await page.clock.install()
  const start = await page.evaluate(() => performance.now())
  cacheTimeline = { sampleAt: start, attemptedAt: start, readyAt: null }
  await page.getByRole('button', { name: it['network.refresh'], exact: true }).click()
  for (let step = 0; step < 48; step++) {
    await page.clock.runFor(500)
    // Flush browser/network IPC in real time; page.waitForTimeout uses the
    // virtual clock and could otherwise outrun the mocked async response.
    await new Promise(resolve => setTimeout(resolve, 50))
    assert.equal(await page.getByText('192.0.2.103', { exact: true }).count(), 1, `cache-refresh flicker at ${step * 500}ms`)
    assert.equal(await page.locator('.cc2-network [role="status"]').count(), 0, `healthy async refresh shown as unavailable at ${step * 500}ms`)
  }
  assert(cacheSamples.some(s => s.refreshing), 'fixture never exercised the stale-while-refreshing response')
  assert(cacheSamples.some(s => s.at - s.age > start), 'fixture never returned a newly collected sample')
  cacheTimeline = null
  await page.getByRole('tab', { name: it['settings.connection'], exact: true }).click()
  await page.locator('.cc2-network').waitFor({ state: 'detached' })
  const before = calls
  await page.clock.runFor(11000)
  assert.equal(calls, before, 'network polling kept running outside the tab')
  assert.deepEqual(mutations, [], 'network page sent a mutation request')
  assert.deepEqual(errors, [])
  assert.deepEqual(outside, [])
  const result = { production_ui: true, nine_languages: true, widths: [320, 390, 768, 1440],
    actual_control_paths: true, failures_and_stale_data: true, literal_ssid: true,
    outside_tab_polling_stopped: true, real_async_cache_timeline: true, network_mutations: mutations, browser_errors: errors }
  await writeFile(path.join(output, 'validation.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
} finally {
  await browser.close()
}
