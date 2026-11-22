import assert from 'node:assert/strict'
import { createServer } from 'vite'
import { chromium } from 'playwright'
import { fileURLToPath } from 'node:url'
import { readFile } from 'node:fs/promises'
import path from 'node:path'

const root = fileURLToPath(new URL('..', import.meta.url))
const locale = async code => JSON.parse(await readFile(path.join(root, `public/locales/${code}.json`), 'utf8'))
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1', allowedHosts: true } })
let browser
const errors = []
try {
  await server.listen()
  const localUrl = `${server.resolvedUrls.local[0]}#settings/integrations`
  const remoteUrl = localUrl.replace('127.0.0.1', 'printer.test')
  // A page opened through an IPv6 address: the browser keeps the brackets in location.hostname, so the example stays a valid host:port.
  const ipv6Url = localUrl.replace('127.0.0.1', '[fd00::1]')
  browser = await chromium.launch({ headless: true, args: ['--host-resolver-rules=MAP printer.test 127.0.0.1'], ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--host-resolver-rules=MAP printer.test 127.0.0.1'] } : {}) })
  // A page opened through a local address shows a typical printer address and says so; a real address is used as it is.
  for (const [code, url, host, local] of [['en', localUrl, '192.168.1.50', true], ['de', localUrl, '192.168.1.50', true], ['en', remoteUrl, 'printer.test', false], ['en', ipv6Url, '[fd00::1]', false]]) {
    const text = await locale(code)
    const label = `${code}/${local ? 'local' : 'address'}`
    const context = await browser.newContext({ viewport: { width: 1440, height: 1100 }, permissions: ['clipboard-read', 'clipboard-write'] })
    const page = await context.newPage()
    page.on('pageerror', e => errors.push(e.message))
    // The IPv6 address does not exist here: its requests are answered by the local server.
    await context.route(u => u.href.includes('[fd00::1]'), async route => route.fulfill({ response: await route.fetch({ url: route.request().url().replace('[fd00::1]', '127.0.0.1') }) }))
    await page.route('**/api/preferences', r => r.fulfill({ json: { language: code, theme: 'dark' } }))
    await page.goto(url)
    const toggle = page.locator('[data-elegooslicer-toggle]')
    await toggle.waitFor()
    assert.equal(await page.getByText(text['settings.elegooslicer_title'], { exact: true }).count(), 1, `${label}: title`)
    assert.equal(await page.locator('[data-elegooslicer-guide]').count(), 0, `${label}: guide starts closed`)
    assert.equal(await toggle.getAttribute('aria-expanded'), 'false')
    await toggle.click()
    const guide = page.locator('[data-elegooslicer-guide]')
    await guide.waitFor()
    assert.equal(await toggle.getAttribute('aria-expanded'), 'true')
    assert.equal(await toggle.innerText(), text['settings.elegooslicer_hide_guide'])
    const body = await guide.innerText()
    for (const key of ['intro', 'step_copy', 'step_open', 'path_other', 'step_edit', 'example_caption', 'edit_note', 'step_choose', 'step_print', 'warning']) {
      assert(body.includes(text[`settings.elegooslicer_${key}`]), `${label}: ${key} text shown`)
    }
    // Example with the numbers to change: the address this page was opened with, port 8081, five lines.
    const example = await page.locator('[data-elegooslicer-example]').innerText()
    const added = [
      '"host_type": "octoprint",',
      `"print_host": "${host}:8081",`,
      `"print_host_webui": "http://${host}:8081",`,
      '"bbl_use_print_host_webui": "1",',
      '"printhost_authorization_type": "key",',
    ]
    for (const line of added) assert(example.includes(line), `${label}: example line ${line}`)
    assert(example.trimStart().startsWith('{'), `${label}: example starts with the opening brace`)
    assert(body.includes('~/Library/Application Support/ElegooSlicer/user/default/machine/'), `${label}: macOS folder example`)
    // Opened through a local address: the hint is shown, with the address of a real printer it is not.
    assert.equal(await page.locator('[data-elegooslicer-localhost]').count(), local ? 1 : 0, `${label}: localhost hint`)
    if (local) assert((await guide.innerText()).includes(text['settings.elegooslicer_localhost_hint']), `${label}: localhost hint text`)
    // The warning about the profile falling back to the default must be visible and marked as a note.
    const warning = page.locator('[data-elegooslicer-warning]')
    assert.equal(await warning.getAttribute('role'), 'note')
    assert.equal(await warning.isVisible(), true)
    // Copy puts exactly the five lines on the clipboard.
    await page.locator('[data-elegooslicer-copy]').click()
    await page.getByText(text['settings.elegooslicer_copied'], { exact: true }).waitFor()
    // Over plain HTTP (the printer's page) the clipboard API is missing and copyText uses its fallback: only read back where it exists.
    if (await page.evaluate(() => Boolean(navigator.clipboard))) {
      assert.equal(await page.evaluate(() => navigator.clipboard.readText()), added.join('\n'), `${label}: clipboard`)
    }
    for (const width of [1440, 768, 390]) {
      await page.setViewportSize({ width, height: 1100 })
      // The guide itself and its example block stay inside the window; long lines scroll inside the example only.
      assert(await page.evaluate(() => {
        const box = e => document.querySelector(e).getBoundingClientRect()
        return box('[data-elegooslicer-guide]').right <= innerWidth + 1 && box('[data-elegooslicer-example]').right <= innerWidth + 1
      }), `${label}: guide wider than the window at ${width}`)
    }
    // Accessibility of the open guide in both colour schemes, desktop and phone width (axe, when its path is given like in the other tests).
    if (process.env.CC2_AXE_PATH) {
      await page.addScriptTag({ path: process.env.CC2_AXE_PATH })
      await page.mouse.move(0, 0)
      await page.getByText(text['settings.elegooslicer_copied'], { exact: true }).waitFor({ state: 'hidden', timeout: 15000 })
      for (const scheme of ['dark', 'light']) for (const width of [1440, 390]) {
        await page.setViewportSize({ width, height: 1100 })
        await page.evaluate(t => { document.documentElement.dataset.theme = t }, scheme)
        await page.waitForTimeout(600) // colour transitions of the theme change must be finished before axe measures
        const violations = await page.evaluate(async () => (await axe.run(document.querySelector('[data-elegooslicer-guide]'))).violations.map(v => ({ id: v.id, nodes: v.nodes.map(n => n.target) })))
        assert.deepEqual(violations, [], `${label}: axe ${scheme} ${width}`)
      }
    }
    await toggle.click()
    assert.equal(await page.locator('[data-elegooslicer-guide]').count(), 0, `${label}: guide closes again`)
    await context.close()
  }
  assert.deepEqual(errors, [])
  console.log('PASS ElegooSlicer guide: closed by default, shows every step, example with the page address and port 8081, macOS folder, profile fall-back warning, copy, English and German, localhost hint only for local addresses, fits the window')
} finally { await browser?.close(); await server.close() }
