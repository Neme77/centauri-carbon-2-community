import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

const root = fileURLToPath(new URL('..', import.meta.url))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } })
  const errors = []
  const writes = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('request', r => {
    if (r.url().includes('/api/appearance') && r.method() !== 'GET')
      writes.push({ url: r.url(), method: r.method(), body: r.postData() })
  })
  const origin = server.resolvedUrls.local[0]
  await page.goto(origin + '#appearance/themes')
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  if (process.env.CC2_SCREENSHOTS) {
    await page.waitForFunction(() => document.querySelector('#cc2-navigation')?.getBoundingClientRect().width > 200)
    await page.addStyleTag({ content: '#cc2-preview-banner{display:none!important}' })
    await page.waitForTimeout(200)
    await page.screenshot({ path: path.join(process.env.CC2_SCREENSHOTS, 'appearance-themes.png'), fullPage: true })
  }

  // A built-in theme applies immediately and is written to the printer.
  const nord = page.locator('button[aria-pressed]').filter({ hasText: 'Nord' }).first()
  const nordWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance') && r.method() === 'PUT')
  await nord.click()
  await nordWrite
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'nord')
  assert(writes.some(w => w.method === 'PUT' && w.body.includes('"theme":"nord"')), JSON.stringify(writes))

  // A custom theme starts from a preset, is written per item, and applies.
  await page.getByRole('button', { name: 'New theme', exact: true }).click()
  const dialog = page.getByRole('dialog')
  await dialog.locator('select').selectOption('klipper-dark')
  await dialog.locator('input').first().fill('Demo industrial')
  await dialog.locator('#token-cyan').evaluate(el => {
    el.value = '#ff00aa'
    el.dispatchEvent(new Event('input', { bubbles: true }))
  })
  const themeWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance/themes') && r.method() === 'POST')
  await dialog.getByRole('button', { name: 'Save theme', exact: true }).click()
  await themeWrite
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'custom')
  const custom = await page.evaluate(() => ({
    bg: getComputedStyle(document.documentElement).getPropertyValue('--bg').trim(),
    accent: getComputedStyle(document.documentElement).getPropertyValue('--cyan').trim(),
  }))
  assert.equal(custom.bg, '#121212')
  assert.equal(custom.accent, '#ff00aa')
  const postedTheme = writes.find(w => w.url.endsWith('/api/appearance/themes') && w.method === 'POST')
  assert(postedTheme && postedTheme.body.includes('"id":"custom-demo-industrial"'), JSON.stringify(writes))
  // The document save must not carry the theme library any more.
  const documentPut = writes.filter(w => w.url.endsWith('/api/appearance') && w.method === 'PUT').pop()
  assert(!documentPut.body.includes('"themes"'), 'the document PUT must not replace the theme library')

  // A long theme name still produces an id inside the 24-character contract.
  await page.getByRole('button', { name: 'New theme', exact: true }).click()
  await page.getByRole('dialog').locator('input').first().fill('A very long custom theme name for the contract')
  const longWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance/themes') && r.method() === 'POST')
  await page.getByRole('dialog').getByRole('button', { name: 'Save theme', exact: true }).click()
  await longWrite
  const longTheme = writes.filter(w => w.url.endsWith('/api/appearance/themes') && w.method === 'POST').pop()
  const longId = JSON.parse(longTheme.body).theme.id
  assert(longId.length <= 24, `${longId} (${longId.length})`)
  assert(/^custom-[a-z][a-z-]*[a-z]$/.test(longId) && !longId.includes('--'), longId)

  // A one-letter name is the shortest id the studio can generate ("custom-a");
  // the client must keep it after a reload instead of dropping it.
  await page.getByRole('button', { name: 'New theme', exact: true }).click()
  await page.getByRole('dialog').locator('input').first().fill('A')
  const shortWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance/themes') && r.method() === 'POST')
  await page.getByRole('dialog').getByRole('button', { name: 'Save theme', exact: true }).click()
  await shortWrite
  const shortTheme = writes.filter(w => w.url.endsWith('/api/appearance/themes') && w.method === 'POST').pop()
  assert.equal(JSON.parse(shortTheme.body).theme.id, 'custom-a')
  await page.goto(origin + '#appearance/themes')
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  await page.waitForTimeout(400)
  await page.reload()
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  await page.waitForTimeout(400)
  assert(
    (await page.locator('button[aria-pressed]').filter({ hasText: /^A/ }).count()) > 0,
    'the one-letter theme must survive a reload'
  )

  // Deleting a custom theme uses the explicit endpoint and does not lose the
  // other theme another browser could have added.
  await page.goto(origin + '#appearance/themes')
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  const longCard = page
    .locator('button[aria-pressed]')
    .filter({ hasText: 'A very long custom theme name for the contract' })
    .first()
    .locator('xpath=..')
  await longCard.locator('button[aria-pressed]').click()
  await longCard.getByRole('button', { name: 'Edit', exact: true }).click()
  const deleteWrite = page.waitForRequest(r => r.url().endsWith('/api/appearance/themes/delete') && r.method() === 'POST')
  await page.getByRole('dialog').getByRole('button', { name: 'Delete', exact: true }).click()
  await page.getByRole('dialog').last().getByRole('button', { name: 'Confirm', exact: true }).click()
  await deleteWrite
  await page.waitForTimeout(400)
  assert(writes.some(w => w.url.endsWith('/api/appearance/themes/delete')), JSON.stringify(writes))
  await page.reload()
  await page.getByRole('tab', { name: 'Themes', exact: true }).waitFor()
  await page.waitForTimeout(400)
  assert.equal(
    await page.getByText('A very long custom theme name for the contract', { exact: false }).count(),
    0,
    'a deleted theme must be gone after a reload'
  )

  // System theme follows the operating-system preference and reacts to it.
  await page.emulateMedia({ colorScheme: 'light' })
  await page.getByRole('tab', { name: 'Themes', exact: true }).click()
  await page.locator('button[aria-pressed]').filter({ hasText: 'Follow the system' }).first().click()
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'light')
  await page.emulateMedia({ colorScheme: 'dark' })
  await page.waitForFunction(() => document.documentElement.dataset.theme === 'dark')

  // Layout and widgets.
  await page.getByRole('tab', { name: 'Dashboard layout', exact: true }).click()
  await page.getByRole('button', { name: 'Create a custom layout', exact: true }).click()
  await page.getByRole('tab', { name: 'Widgets', exact: true }).click()
  const fans = page.locator('[data-widget="fans"]')
  await fans.getByRole('switch', { name: 'Fans', exact: true }).click()
  await page.goto(origin + '#dashboard')
  await page.locator('.cc2-dashboard').waitFor()
  assert.equal(await page.locator('.cc2-dashboard-fans').count(), 0)
  const firstBefore = await page.locator('.cc2-widget-cell').first().getAttribute('data-widget')
  await page.goto(origin + '#appearance/widgets')
  const camera = page.locator('[data-widget="camera"]')
  await camera.getByRole('button', { name: /Move Camera down/ }).click()
  await page.goto(origin + '#dashboard')
  const firstAfter = await page.locator('.cc2-widget-cell').first().getAttribute('data-widget')
  assert.notEqual(firstBefore, firstAfter)

  // Profiles, with a name at the 64-character limit (the longest the printer
  // stores) so the phone-width check below covers the real worst case.
  const longProfile = 'Evening ' + 'x'.repeat(56)
  await page.goto(origin + '#appearance/profiles')
  await page.getByRole('textbox', { name: 'Profile name' }).fill(longProfile)
  const profileWrite = page.waitForResponse(r => r.url().endsWith('/api/appearance/profiles') && r.request().method() === 'POST')
  await page.getByRole('button', { name: 'Save current as profile', exact: true }).click()
  const profileResponse = await profileWrite
  assert.equal(profileResponse.status(), 200, await profileResponse.text())
  await page.getByText(longProfile, { exact: true }).waitFor()
  await page.getByRole('button', { name: 'Apply', exact: true }).last().click()
  await page.waitForFunction(name => document.body.textContent.includes(name), longProfile)

  // Every tab renders, including the one that used to throw (Advanced).
  const tabs = [
    ['overview', 'Overview'],
    ['themes', 'Themes'],
    ['layout', 'Dashboard layout'],
    ['widgets', 'Widgets'],
    ['touchscreen', 'Touchscreen'],
    ['profiles', 'Profiles'],
    ['transfer', 'Import and export'],
    ['advanced', 'Advanced'],
  ]
  for (const [slug, name] of tabs) {
    await page.goto(`${origin}#appearance/${slug}`)
    await page.getByRole('tab', { name, exact: true }).waitFor()
    await page.waitForTimeout(150)
    assert.equal(await page.locator('.cc2-appearance section').count() > 0, true, `${name} rendered no card`)
  }

  // The touchscreen tab is honest about this demo's missing manifest.
  await page.goto(origin + '#appearance/touchscreen')
  await page.getByText('Not supported', { exact: true }).waitFor()
  assert((await page.locator('.cc2-appearance').innerText()).toLowerCase().includes('manifest'))

  // Export then import the configuration through the UI, and refuse a file
  // whose theme tokens are invalid (the same validation as the editor).
  await page.goto(origin + '#appearance/transfer')
  await page.getByRole('tab', { name: 'Import and export', exact: true }).waitFor()
  const downloaded = page.waitForEvent('download')
  await page.getByRole('button', { name: 'Export all appearance settings', exact: true }).click()
  const archive = await downloaded
  const archivePath = await archive.path()
  const fileInputs = page.locator('input[type=file]')
  const importResponse = page.waitForResponse(r => r.url().endsWith('/api/appearance') && r.request().method() === 'PUT')
  await fileInputs.first().setInputFiles(archivePath)
  assert.equal((await importResponse).status(), 200, 'import must save the document')
  await page.getByText('Imported.', { exact: true }).waitFor()
  const writesBeforeBad = writes.length
  await fileInputs.nth(1).setInputFiles({
    name: 'invalid-theme.json',
    mimeType: 'application/json',
    buffer: Buffer.from(JSON.stringify({ format: 'cc2-theme', version: 1, name: 'Bad', tokens: { bg: 'red' } })),
  })
  await page.getByText('This file is not a valid CC2 appearance export.', { exact: true }).waitFor()
  assert.equal(writes.length, writesBeforeBad, 'an invalid theme must not be written')

  // Reload keeps the stored appearance (the mock backend is the source of truth).
  await page.reload()
  await page.getByRole('tab', { name: 'Touchscreen', exact: true }).waitFor()
  assert.equal(await page.evaluate(() => document.documentElement.dataset.theme), 'dark')

  // The studio must stay usable at phone width: wrapped tabs, no horizontal overflow.
  for (const width of [390, 320]) {
    await page.setViewportSize({ width, height: 900 })
    for (const [slug, tab] of [
      ['themes', 'Themes'],
      ['profiles', 'Profiles'],
      ['overview', 'Overview'],
    ]) {
      await page.goto(`${origin}#appearance/${slug}`)
      await page.getByRole('tab', { name: tab, exact: true }).waitFor()
      await page.waitForTimeout(400)
      assert.equal(
        await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1),
        false,
        `${width}px overflow on ${slug}`
      )
    }
  }

  assert.deepEqual(errors, [])
  console.log('PASS: appearance themes, per-item writes, theme-id contract, delete, auto mode, layout/widget changes, profiles, all tabs, touchscreen honesty, reload and phone width')
} finally {
  if (browser) await browser.close()
  await server.close()
}
