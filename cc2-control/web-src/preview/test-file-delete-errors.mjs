import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'

const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const origin = server.resolvedUrls.local[0]
  for (const language of ['en', 'it', 'hu', 'fr', 'es', 'pl', 'cs', 'ru', 'zh']) {
    const labels = JSON.parse(await readFile(path.join(root, `public/locales/${language}.json`), 'utf8'))
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
    const errors = []
    page.on('pageerror', e => errors.push(e.message))
    await page.route('**/api/preferences', route => route.fulfill({ json: { language, theme: 'dark' } }))
    await page.route('**/i18n/*.json', async route => {
      const code = new URL(route.request().url()).pathname.split('/').pop()
      await route.fulfill({ contentType: 'application/json', body: await readFile(path.join(root, 'public/locales', code), 'utf8') })
    })
    let code = 'file_delete_active_print', malformed = false, deleted = 0, requests = 0, bulk = false
    await page.route('**/api/gcode-files', route => route.fulfill({ json: {
      internal: { available: true, files: ['one.gcode', 'two.gcode'].slice(deleted).map(name => ({ path: name, size: 20, modified: 1700000000 })) },
      usb: { files: [] },
    } }))
    await page.route('**/api/gcode-files/delete', route => {
      requests++
      if (bulk && requests === 1) { deleted++; return route.fulfill({ json: { deleted: true } }) }
      return route.fulfill({ status: 409, contentType: 'application/json', body: malformed ? '<invalid>' : JSON.stringify({ error: 'RAW ENGLISH MUST NOT APPEAR', ...(code ? { code } : {}) }) })
    })
    await page.goto(origin + '#files')
    await page.waitForFunction(lang => document.documentElement.lang === lang, language)
    const row = page.locator('.cc2-file-row').first()
    const confirm = () => page.getByRole('dialog').getByRole('button', { name: labels['common.confirm'], exact: true }).click()
    const cases = Object.keys(labels).filter(key => key.startsWith('files.delete_reason_')).map(key => [key.replace('files.delete_reason_', 'file_delete_'), key])
    cases.push(['unknown_server_code', 'files.delete_reason_failed'], ['toString', 'files.delete_reason_failed'], ['', 'files.delete_reason_failed'])
    for (const [next, key] of cases) {
      code = next
      await row.getByRole('button', { name: labels['common.delete'], exact: true }).click()
      await confirm()
      await page.getByText(labels['files.delete_failed_error'].replace('{error}', labels[key]), { exact: true }).waitFor()
      assert.equal(await page.getByText('RAW ENGLISH MUST NOT APPEAR', { exact: false }).count(), 0)
      assert.equal(await page.locator('.cc2-file-row').count(), 2, 'refusal leaves files intact')
    }
    malformed = true
    await row.getByRole('button', { name: labels['common.delete'], exact: true }).click()
    await confirm()
    await page.getByText(labels['files.delete_failed_error'].replace('{error}', labels['files.delete_reason_failed']), { exact: true }).waitFor()
    malformed = false; code = 'file_delete_active_print'; requests = 0; bulk = true
    await page.locator('.cc2-file-row input[type=checkbox]').nth(0).check()
    await page.locator('.cc2-file-row input[type=checkbox]').nth(1).check()
    await page.getByRole('button', { name: labels['files.delete_selected'], exact: true }).click()
    await confirm()
    await page.getByText(labels['files.stopped_after_n_files_error'].replace('{n}', '1').replace('{error}', labels['files.delete_reason_active_print']), { exact: true }).waitFor()
    assert.equal(requests, 2, 'bulk deletion stops on the first refusal')
    await page.waitForFunction(() => document.querySelectorAll('.cc2-file-row').length === 1)
    assert.deepEqual(errors, [])
    await page.close()
    console.log(`PASS: ${language} single/bulk deletion codes, unknown/missing codes, malformed reply, unchanged files`)
  }
} finally { await browser?.close(); await server.close() }
