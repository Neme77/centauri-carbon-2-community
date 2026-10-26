import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdir, readFile } from 'node:fs/promises'
const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []; page.on('pageerror', e => errors.push(e.message))
  let downloads = 0, installs = 0, checks = 0
  let preferenceLanguage = 'en'
  const localized = {
    en: 'Stable fixture — retained settings',
    it: 'Note italiane: impostazioni conservate',
    es: 'Notas en español: configuración conservada',
    fr: 'Notes françaises : paramètres conservés',
    pl: 'Polskie informacje: ustawienia zachowane',
    cs: 'České poznámky: nastavení zachováno',
    hu: 'Magyar megjegyzések: beállítások megőrizve',
    ru: 'Русские примечания: настройки сохранены',
    zh: '中文发行说明：保留设置',
  }
  const status = { enabled: true, ready: true, busy: false, installed: '1.2.8', firmware: '4.4', job: '', phase: 'none', version: '' }
  const index = { schema: 1, expires: Math.floor(Date.now()/1000)+86400, releases: [
    { version: '1.2.9', firmware: '4.4', channel: 'stable', url: 'https://github.com/Neme77/centauri-carbon-2-community/releases/download/control-1.2.9/Control.cc2update', size: 1500000, notes: localized.en, notes_i18n: { ...localized } },
    { version: '9.0.0', firmware: '4.3', channel: 'stable', url: 'https://github.com/Neme77/centauri-carbon-2-community/releases/download/control-9.0.0/Control.cc2update', size: 1500000, notes: 'WRONG FIRMWARE — must be hidden' },
    { version: '1.3.0', firmware: '4.4', channel: 'canvas-uds', url: 'https://github.com/Neme77/centauri-carbon-2-community/releases/download/control-1.3.0/Control.cc2update', size: 1500000, notes: 'Experimental fixture' },
  ] }
  let cache = { schema: 1, expires: 0, releases: [] }
  await page.route('**/i18n/*.json', async r => {
    const code = new URL(r.request().url()).pathname.match(/^\/i18n\/(en|it|es|fr|pl|cs|hu|ru|zh)\.json$/)?.[1]
    assert(code, 'unexpected locale request')
    return r.fulfill({ body: await readFile(path.join(root, `public/locales/${code}.json`), 'utf8'), contentType: 'application/json' })
  })
  await page.route('**/api/preferences', r => {
    if (r.request().method() === 'PUT') preferenceLanguage = r.request().postDataJSON().language || preferenceLanguage
    return r.fulfill({ json: { language: preferenceLanguage, theme: 'dark' } })
  })
  await page.route('https://raw.githubusercontent.com/**/update-channel.cc2catalog', r => { checks++; return r.fulfill({ body: Buffer.from('signed-catalog-fixture'), contentType: 'application/octet-stream', headers: { 'Access-Control-Allow-Origin': '*' } }) })
  await page.route('**/api/app-update/status', r => r.fulfill({ json: status }))
  await page.route('**/api/app-update/catalog', r => {
    if (r.request().method() === 'POST') cache = index
    return r.fulfill({ json: cache })
  })
  await page.route('**/api/app-update/download', r => {
    assert.equal(r.request().headers()['x-cc2-request'], '1')
    if (r.request().headers()['x-cc2-lan'] !== 'test-confirmation') return r.fulfill({ status: 401, json: { error: 'LAN code rejected' } })
    assert.equal(r.request().postData(), index.releases[0].url)
    downloads++; Object.assign(status, { phase: 'ready', job: 'a'.repeat(32), version: '1.2.9' })
    return r.fulfill({ status: 201, json: { verified: true, job: status.job } })
  })
  await page.route('**/api/app-update/install', r => {
    assert.equal(r.request().headers()['x-cc2-lan'], 'test-confirmation')
    assert.equal(r.request().postData(), 'a'.repeat(32))
    installs++; Object.assign(status, { phase: 'complete', installed: '1.2.9' })
    return r.fulfill({ status: 202, json: { accepted: true } })
  })
  await page.goto(server.resolvedUrls.local[0] + '#settings')
  await page.getByRole('tab', { name: 'Updates', exact: true }).click()
  await page.getByText('Compatible update available: 1.2.9', { exact: true }).waitFor()
  // Name all three supported Community Firmware releases, in every translated catalog.
  for (const lang of ['en', 'hu', 'cs', 'es', 'fr', 'it', 'pl', 'ru', 'zh']) {
    const dict = JSON.parse(await readFile(path.join(root, `public/locales/${lang}.json`)))
    assert.match(dict['updates.requirements'], /V4\.4\/V4\.4\.1\/V4\.4\.2/, lang + ' update requirements are out of date')
  }
// DamiBFryta: the native chooser's visible field and both cursor areas must agree.
  await page.locator('details summary').click()
  const localFile = page.getByLabel('Upload a signed package from your computer', { exact: true })
  await localFile.waitFor()
  for (const theme of ['dark', 'light']) {
    await page.evaluate(value => { document.documentElement.dataset.theme = value }, theme)
    const appearance = await localFile.evaluate(input => {
      const field = getComputedStyle(input)
      const button = getComputedStyle(input, '::file-selector-button')
      return { fieldCursor: field.cursor, buttonCursor: button.cursor,
        fieldBorder: parseFloat(field.borderTopWidth), buttonBorder: parseFloat(button.borderTopWidth),
        background: button.backgroundColor, color: button.color }
    })
    assert.equal(appearance.fieldCursor, 'pointer')
    assert.equal(appearance.buttonCursor, 'pointer')
    assert(appearance.fieldBorder > 0 && appearance.buttonBorder > 0, 'field and button must have visible borders')
    assert.notEqual(appearance.background, appearance.color)
    if (process.env.CC2_SCREENSHOTS) {
      await mkdir(process.env.CC2_SCREENSHOTS, { recursive: true })
      await localFile.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, `update-file-${theme}.png`) })
    }
    for (const position of [{ x: 35, y: 18 }, { x: 350, y: 18 }]) {
      const chooserPromise = page.waitForEvent('filechooser')
      await localFile.click({ position })
      const chooser = await chooserPromise
      assert.equal(await chooser.element().getAttribute('accept'), '.cc2update')
      await chooser.setFiles([])
    }
  }
  await page.evaluate(() => { document.documentElement.dataset.theme = 'dark' })
  // Dismissing a native chooser can leave the browser window inactive in headless CI.
  // Restore the page before testing real keyboard activation on the input itself.
  await page.bringToFront()
  const keyboardChooser = page.waitForEvent('filechooser')
  await localFile.press('Space')
  await (await keyboardChooser).setFiles({ name: 'trial.cc2update', mimeType: 'application/octet-stream', buffer: Buffer.alloc(1200) })
  assert.equal(await localFile.evaluate(input => input.files[0].name), 'trial.cc2update')
  assert.equal(downloads, 0, 'choosing a file must not download or install')
  assert.equal(installs, 0)
  const verifyFile = page.getByRole('button', { name: 'Verify local package', exact: true })
  assert.equal(await verifyFile.isEnabled(), false, 'LAN confirmation still required')
  status.busy = true
  await page.waitForFunction(() => document.querySelector('.cc2-update-file')?.disabled)
  assert.equal(await localFile.evaluate(input => getComputedStyle(input).cursor), 'not-allowed')
  assert.equal(await localFile.evaluate(input => getComputedStyle(input, '::file-selector-button').cursor), 'not-allowed')
  status.busy = false
  await page.waitForFunction(() => !document.querySelector('.cc2-update-file')?.disabled)
  for (const width of [1440, 768, 390]) {
    await page.setViewportSize({ width, height: 1000 })
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'local file input overflow ' + width)
    if (width === 390 && process.env.CC2_SCREENSHOTS) {
      await localFile.evaluate(input => input.scrollIntoView({ block: 'center' }))
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))))
      await localFile.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, 'update-file-mobile.png') })
    }
  }
  await page.setViewportSize({ width: 1440, height: 1000 })
  assert.equal(checks, 1)
  assert.equal(await page.getByText('WRONG FIRMWARE — must be hidden').count(), 0)
  const download = page.getByRole('button', { name: 'Download and verify', exact: true })
  const install = page.getByRole('button', { name: 'Install verified update', exact: true })
  assert.equal(await download.isEnabled(), false)
  assert.equal(await install.isEnabled(), false)
  await page.getByLabel('LAN Access Code', { exact: true }).fill('bad-code')
  await download.click(); await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
  await page.getByRole('alert').filter({hasText:'LAN code rejected'}).waitFor()
  assert.equal(downloads, 0); assert.equal(installs, 0)
  await page.getByLabel('LAN Access Code', { exact: true }).fill('test-confirmation')
  await download.click(); await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
  await page.getByText('Selected version: 1.2.9', { exact: true }).waitFor()
  assert.equal(downloads, 1); assert.equal(installs, 0, 'verification must not install')
  assert.equal(await install.isEnabled(), true)
  await install.click(); await page.getByRole('dialog').getByRole('button',{name:'Cancel',exact:true}).click()
  assert.equal(installs, 0)
  await install.click(); await page.getByRole('dialog').getByRole('button',{name:'Confirm',exact:true}).click()
  await page.getByRole('button',{name:'Reload CC2 Control',exact:true}).waitFor()
  assert.equal(installs, 1)
  assert.equal(await page.getByLabel('LAN Access Code', { exact: true }).inputValue(), '')
  await page.getByText('No newer compatible update published.', { exact: true }).waitFor()
  assert.equal(await page.getByText(localized.en, { exact: true }).count(), 0, 'installed release notes must disappear')
  assert.equal(await page.getByLabel('Update channel', { exact: true }).count(), 0)
  assert.equal(await page.getByText('Experimental fixture', { exact: true }).count(), 0)
  for (const width of [1440,768,390]) {
    await page.setViewportSize({width,height:1000})
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'settings overflow '+width)
  }
  await page.setViewportSize({ width: 1440, height: 1000 })
  index.releases[0].version = '1.2.10'
  index.releases[0].url = index.releases[0].url.replace('control-1.2.9', 'control-1.2.10')
  await page.getByRole('button', { name: 'Check for updates', exact: true }).click()
  await page.getByText(localized.en, { exact: true }).waitFor()
  let previous = JSON.parse(await readFile(path.join(root, 'public/locales/en.json')))
  for (const language of Object.keys(localized)) {
    const dict = JSON.parse(await readFile(path.join(root, `public/locales/${language}.json`)))
    await page.getByRole('tab', { name: previous['settings.appearance'], exact: true }).click()
    await page.locator('main select:has(option[value="hu"])').selectOption(language)
    await page.waitForFunction(code => document.documentElement.lang === code, language)
    await page.getByRole('tab', { name: dict['updates.title'], exact: true }).click()
    await page.getByText(dict['updates.requirements'], { exact: true }).waitFor()
    await page.getByText(localized[language], { exact: true }).waitFor()
    if (language !== 'en') assert.equal(await page.getByText(localized.en, { exact: true }).count(), 0)
    assert.equal(checks, 2, 'changing the UI language must not fetch another catalog')
    previous = dict
  }
  index.releases[0].notes_i18n.zh = 42
  await page.getByRole('button', { name: previous['updates.check'], exact: true }).click()
  await page.getByText(localized.en, { exact: true }).waitFor()
  await page.getByText(previous['updates.notes_in_english'], { exact: true }).waitFor()
  delete index.releases[0].notes_i18n
  await page.getByRole('button', { name: previous['updates.check'], exact: true }).click()
  await page.getByText(localized.en, { exact: true }).waitFor()
  await page.getByText(previous['updates.notes_in_english'], { exact: true }).waitFor()
  index.releases[0].version = '1.2.8'
  await page.getByRole('button', { name: previous['updates.check'], exact: true }).click()
  await page.getByText(previous['updates.no_update'], { exact: true }).waitFor()
  assert.equal(await page.getByText(localized.en, { exact: true }).count(), 0, 'older release notes must remain hidden')
  assert.equal(await page.getByText(previous['updates.notes_in_english'], { exact: true }).count(), 0)
  assert.deepEqual(errors, [])
  console.log('PASS: native file chooser mouse/keyboard, light/dark styling, disabled cursors, mobile layout; update auth/confirmations, installed/older note hiding, 9-language selection and labelled fallback')
} finally { await browser?.close(); await server.close() }
