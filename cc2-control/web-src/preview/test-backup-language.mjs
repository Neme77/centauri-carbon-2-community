import assert from 'node:assert/strict'
import { mkdtemp, readFile, writeFile, mkdir, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createServer } from 'node:net'
import { execFile, spawn } from 'node:child_process'
import { promisify } from 'node:util'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url)), backend = path.resolve(root, '..')
const temporary = await mkdtemp(path.join(tmpdir(), 'cc2-backup-browser-'))
const binary = path.join(temporary, 'cc2-control')
await promisify(execFile)('gcc', ['-Os', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-pthread',
  ...['main', 'mqtt', 'console', 'control', 'panda', 'uds'].map(n => path.join(backend, 'src', `${n}.c`)), '-lm', '-o', binary])
const listener = createServer()
await new Promise(resolve => listener.listen(0, '127.0.0.1', resolve))
const port = listener.address().port
await new Promise(resolve => listener.close(resolve))
const service = spawn(binary, ['--port', String(port), '--panda-port', '0', '--web-root', path.join(backend, 'web'),
  '--config', path.join(temporary, 'missing.conf'), '--preferences', path.join(temporary, 'preferences.json'),
  '--presets', path.join(temporary, 'presets.json'), '--plates', path.join(temporary, 'plates.json'),
  '--spools', path.join(temporary, 'spools.json')], { cwd: temporary, stdio: 'ignore' })
const origin = `http://127.0.0.1:${port}`
const screenshots = process.env.CC2_SCREENSHOTS || path.join(temporary, 'screenshots')
await mkdir(screenshots, { recursive: true })
let browser
try {
  for (let i = 0; i < 100; i++) {
    try { if ((await fetch(`${origin}/api/health`)).ok) break } catch {}
    await new Promise(resolve => setTimeout(resolve, 50))
  }
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, acceptDownloads: true })
  const errors = []; page.on('pageerror', e => errors.push(e.message))
  const en = JSON.parse(await readFile(path.join(root, 'public/locales/en.json'), 'utf8'))
  await page.route('**/api/setup', route => route.fulfill({ json: { configured: true, required: false } }))
  await page.route('**/api/preferences', route => route.fulfill({ json: { language: 'en', theme: 'dark' } }))
  let received = [], uploaded, starts = 0, restores = [], cancellations = 0, rejectRestore = true
  const token = 'a'.repeat(32)
  await page.route('**/api/backup/import/*', async route => {
    const request = route.request(), operation = new URL(request.url()).pathname.split('/').pop()
    assert.equal(request.method(), 'POST')
    assert.equal(request.headers()['x-cc2-request'], '1')
    assert.equal(request.headers()['x-cc2-lan'], 'TEST-LAN-CODE')
    if (operation === 'start') {
      starts++; received = []; uploaded = Number(request.postData())
      return route.fulfill({ json: { token, chunk_size: 8192 } })
    }
    assert.equal(request.headers()['x-cc2-backup'], token)
    if (operation === 'chunk') {
      const offset = received.reduce((n, b) => n + b.length, 0), bytes = request.postDataBuffer()
      assert.equal(Number(request.headers()['x-cc2-offset']), offset)
      assert.ok(bytes.length > 0 && bytes.length <= 8192)
      received.push(bytes)
      return route.fulfill({ json: { received: true } })
    }
    if (operation === 'finish') {
      assert.equal(Buffer.concat(received).length, uploaded)
      return route.fulfill({ json: { token, version: '1.2.15', created: 1700000000, sections: 13,
        plates: 2, meshes: 1, nozzles: 1, spools: 1, events: 5 } })
    }
    if (operation === 'restore') {
      if (rejectRestore) return route.fulfill({ status: 409, json: { error: 'Simulated edit conflict' } })
      restores.push(Number(request.postData()))
      return route.fulfill({ json: { restored: true, physical_verification_required: true } })
    }
    if (operation === 'cancel') { cancellations++; return route.fulfill({ json: { cancelled: true } }) }
    throw Error('Unexpected restore operation')
  })
  await page.goto(origin + '/#settings/backup')
  await page.getByRole('heading', { name: en['backup.restore'], exact: true }).waitFor()
  const downloadEvent = page.waitForEvent('download')
  await page.getByRole('button', { name: en['backup.download'], exact: true }).click()
  const download = await downloadEvent, saved = path.join(temporary, 'downloaded.json')
  await download.saveAs(saved)
  const exported = await readFile(saved)
  const data = JSON.parse(exported)
  assert.equal(data.format, 'cc2-control-backup')
  assert.ok(!exported.includes(Buffer.from('TEST-LAN-CODE')))
  assert.match(download.suggestedFilename(), /^CC2-Control-backup-\d{4}-\d{2}-\d{2}\.json$/)
  const input = page.locator('#backup-file')
  assert.ok(await input.isDisabled(), 'LAN confirmation precedes import upload')
  await page.locator('#backup-lan').fill('TEST-LAN-CODE')
  const payload = Buffer.concat([exported, Buffer.alloc(10000, 32)])
  const file = { name: 'my-backup.json', mimeType: 'application/json', buffer: payload }
  await input.setInputFiles(file)
  await page.getByRole('heading', { name: en['backup.preview'], exact: true }).waitFor()
  assert.deepEqual(Buffer.concat(received), payload, 'Upload preserves exact UTF-8 bytes')
  assert.ok(await page.getByLabel(en['backup.materials'], { exact: false }).isDisabled())
  await page.getByLabel(en['backup.interface'], { exact: true }).uncheck()
  await page.getByLabel(en['backup.spools'], { exact: true }).uncheck()
  await page.screenshot({ path: path.join(screenshots, 'backup-preview-desktop.png'), fullPage: true })
  await page.setViewportSize({ width: 390, height: 900 })
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))
  await page.screenshot({ path: path.join(screenshots, 'backup-preview-mobile.png'), fullPage: true })
  const restore = page.getByRole('button', { name: en['backup.restore_selected'], exact: true })
  await restore.click()
  await page.getByRole('dialog').getByRole('button', { name: en['common.cancel'], exact: true }).click()
  assert.deepEqual(restores, [])
  await restore.click()
  await page.getByRole('dialog').getByRole('button', { name: en['common.confirm'], exact: true }).click()
  await page.getByRole('alert').filter({ hasText: 'Simulated edit conflict' }).waitFor()
  assert.deepEqual(restores, [])
  rejectRestore = false
  await restore.click()
  await page.getByRole('dialog').getByRole('button', { name: en['common.confirm'], exact: true }).click()
  await page.getByText(en['backup.restored'], { exact: true }).waitFor()
  assert.deepEqual(restores, [4], 'Only checked, present categories are restored')
  assert.equal(await page.locator('#backup-lan').inputValue(), '')
  assert.ok(!(await page.evaluate(() => Object.values(localStorage).join('|'))).includes('TEST-LAN-CODE'))
  await page.locator('#backup-lan').fill('TEST-LAN-CODE')
  await input.setInputFiles(file) // Same-name selection must trigger another upload.
  await page.getByRole('heading', { name: en['backup.preview'], exact: true }).waitFor()
  await page.getByRole('button', { name: en['common.cancel'], exact: true }).click()
  assert.equal(cancellations, 1)
  const before = starts
  await input.setInputFiles({ name: 'too-big.json', mimeType: 'application/json', buffer: Buffer.alloc(512 * 1024 + 1) })
  await page.getByText(en['backup.invalid_file'], { exact: true }).waitFor()
  assert.equal(starts, before)
  assert.deepEqual(errors, [])
  await page.close()

  // Observe actual DOM headings, not a direct translation-function call.
  for (const code of ['it', 'pl', 'hu']) {
    const context = await browser.newContext()
    await context.addInitScript(() => {
      localStorage.setItem('cc2-language', 'en')
      window.__firstHeadings = []
      new MutationObserver(() => {
        const heading = document.querySelector('main h2')?.textContent
        if (heading) window.__firstHeadings.push(heading)
      }).observe(document, { childList: true, subtree: true, characterData: true })
    })
    const localized = JSON.parse(await readFile(path.join(root, `public/locales/${code}.json`), 'utf8'))
    const localizedPage = await context.newPage()
    await localizedPage.route('**/api/setup', r => r.fulfill({ json: { configured: true, required: false } }))
    await localizedPage.route('**/api/preferences', async r => {
      await new Promise(resolve => setTimeout(resolve, 200))
      return r.fulfill({ json: { language: code, theme: 'light' } })
    })
    await localizedPage.route(`**/i18n/${code}.json`, async r => {
      await new Promise(resolve => setTimeout(resolve, 400))
      return r.fulfill({ json: localized })
    })
    await localizedPage.goto(origin + '/#settings')
    await localizedPage.getByRole('heading', { name: localized['common.settings'], exact: true }).waitFor()
    const headings = await localizedPage.evaluate(() => window.__firstHeadings)
    assert.ok(headings.length && headings.every(h => h === localized['common.settings']), `${code}: no English first render`)
    assert.equal(await localizedPage.evaluate(() => document.documentElement.lang), code)
    await context.close()
  }
  const blocked = await browser.newContext()
  await blocked.addInitScript(() => Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('blocked', 'SecurityError') } }))
  const privatePage = await blocked.newPage()
  const it = JSON.parse(await readFile(path.join(root, 'public/locales/it.json'), 'utf8'))
  await privatePage.route('**/api/setup', r => r.fulfill({ json: { configured: true, required: false } }))
  await privatePage.route('**/api/preferences', r => r.fulfill({ json: { language: 'it', theme: 'dark' } }))
  await privatePage.goto(origin + '/#settings')
  await privatePage.getByRole('heading', { name: it['common.settings'], exact: true }).waitFor()
  await blocked.close()
  const failed = await browser.newContext()
  await failed.addInitScript(() => localStorage.setItem('cc2-language', 'it'))
  const failedPage = await failed.newPage()
  await failedPage.route('**/api/setup', r => r.fulfill({ json: { configured: true, required: false } }))
  await failedPage.route('**/api/preferences', r => r.abort())
  await failedPage.route('**/i18n/it.json', () => {}) // deliberately stalled: AbortController must bound startup
  const began = Date.now()
  await failedPage.goto(origin + '/#settings')
  await failedPage.getByRole('heading', { name: en['common.settings'], exact: true }).waitFor()
  assert.ok(Date.now() - began < 5500, 'Failure must reach a usable page within a bounded startup')
  await failed.close()
  // Every Settings section must be fully visible without horizontal scrolling,
  // including the longer translated Backup label reported on the real printer.
  for (const code of ['en', 'it', 'fr', 'es', 'hu', 'cs', 'pl', 'ru', 'zh']) {
    const context = await browser.newContext({ viewport: { width: 1536, height: 1000 }, reducedMotion: 'reduce' })
    const tabsPage = await context.newPage()
    await tabsPage.route('**/api/setup', r => r.fulfill({ json: { configured: true, required: false } }))
    await tabsPage.route('**/api/preferences', r => r.fulfill({ json: { language: code, theme: 'dark' } }))
    await tabsPage.goto(origin + '/#settings/backup')
    await tabsPage.getByRole('tab').nth(5).waitFor()
    for (const width of [1536, 1024, 768, 390]) {
      await tabsPage.setViewportSize({ width, height: 1000 })
      await tabsPage.waitForFunction(() => innerWidth > 900 || parseFloat(getComputedStyle(document.querySelector('main')).marginLeft) === 0)
      const geometry = await tabsPage.getByRole('tablist').evaluate(bar => {
        const outer = bar.getBoundingClientRect()
        return { fits: bar.scrollWidth <= bar.clientWidth + 1, pageFits: document.documentElement.scrollWidth <= innerWidth,
          tabs: [...bar.querySelectorAll('[role=tab]')].map(button => {
            const r = button.getBoundingClientRect()
            return { contained: r.left >= outer.left - 1 && r.right <= outer.right + 1 && r.top >= outer.top - 1 && r.bottom <= outer.bottom + 1,
              readable: button.scrollWidth <= button.clientWidth + 1 }
          }) }
      })
      assert.equal(geometry.tabs.length, 6)
      assert.ok(geometry.fits && geometry.pageFits && geometry.tabs.every(t => t.contained && t.readable), `${code}/${width}: clipped Settings tab`)
      if (code === 'it' && [1536, 390].includes(width))
        await tabsPage.screenshot({ path: path.join(screenshots, `settings-tabs-it-${width}.png`), fullPage: true, animations: 'disabled' })
    }
    await tabsPage.getByRole('tab').nth(0).click()
    await tabsPage.waitForFunction(() => document.querySelectorAll('[role=tab]')[0]?.getAttribute('aria-selected') === 'true')
    assert.equal(await tabsPage.getByRole('tab').nth(0).getAttribute('aria-selected'), 'true')
    await tabsPage.getByRole('tab').nth(5).click()
    await tabsPage.waitForFunction(() => document.querySelectorAll('[role=tab]')[5]?.getAttribute('aria-selected') === 'true')
    assert.equal(await tabsPage.getByRole('tab').nth(5).getAttribute('aria-selected'), 'true')
    await context.close()
  }
  console.log('PASS: all six Settings tabs fully visible and clickable in nine languages at desktop/tablet/mobile widths')
  console.log('PASS: real backup download, chunked browser import, selective/cancelled/refused restore, mobile layout; first-render Italian/Polish/Hungarian, blocked storage and failed requests')
} finally {
  await browser?.close()
  if (service.exitCode === null && service.signalCode === null) {
    service.kill('SIGTERM')
    await new Promise(resolve => service.once('exit', resolve))
  }
  await rm(temporary, { recursive: true, force: true })
}
