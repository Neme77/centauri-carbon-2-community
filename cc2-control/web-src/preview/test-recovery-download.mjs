import assert from 'node:assert/strict'
import { readFile, rm, mkdtemp, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { createServer as createNetServer } from 'node:net'
import { execFile, spawn } from 'node:child_process'
import { promisify } from 'node:util'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { chromium } from 'playwright'
const root = fileURLToPath(new URL('..', import.meta.url))
const temporary = await mkdtemp(path.join(tmpdir(), 'cc2-browser-recovery-'))
const backend = path.resolve(root, '..')
const binary = path.join(temporary, 'cc2-control')
await promisify(execFile)('gcc', ['-Os', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-pthread', ...['main', 'mqtt', 'console', 'control', 'panda', 'uds'].map(name => path.join(backend, 'src', `${name}.c`)), '-lm', '-o', binary])
await writeFile(path.join(temporary, 'part ü.gcode'), 'G28\n')
await writeFile(path.join(temporary, 'preferences.json'), '{"language":"en","theme":"dark"}')
const probe = createNetServer()
await new Promise(resolve => probe.listen(0, '127.0.0.1', resolve))
const port = probe.address().port
await new Promise(resolve => probe.close(resolve))
const process = spawn(binary, ['--port', String(port), '--panda-port', '0', '--web-root', path.join(backend, 'web'), '--config', path.join(temporary, 'missing.conf'), '--preferences', path.join(temporary, 'preferences.json'), '--gcode-internal', temporary], { stdio: 'ignore' })
const origin = `http://127.0.0.1:${port}`
let browser
try {
  for (let i=0;i<100;i++) {
    try { if ((await fetch(`${origin}/api/health`)).ok) break } catch {}
    await new Promise(resolve => setTimeout(resolve, 50))
  }
  browser = await chromium.launch({ headless: true, ...(globalThis.process.env.CC2_BROWSER_PATH ? { executablePath: globalThis.process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, acceptDownloads: true })
  await context.route('**/api/setup', route => route.fulfill({ json: { required: false } }))
  await context.route('**/api/preferences', route => route.fulfill({ json: { language: 'en', theme: 'dark' } }))
  let armed = false, pending = false, posts = 0
  await context.route('**/api/printer', async route => {
    const response = await route.fetch()
    const data = await response.json()
    data.recovery = { available: armed, reboot_pending: pending, error: 'none' }
    await route.fulfill({ response, json: data })
  })
  await context.route('**/api/recovery/reboot', async route => {
    assert.equal(route.request().method(), 'POST')
    assert.equal(route.request().headers()['x-cc2-request'], '1')
    assert.equal(route.request().postData(), 'REBOOT_AFTER_EMERGENCY')
    assert.ok(armed && !pending)
    posts++; pending = true
    await route.fulfill({ status: 202, contentType: 'application/json', body: '{"accepted":true}' })
  })
  const pages = [await context.newPage(), await context.newPage()]
  for (const page of pages) {
    await page.goto(origin)
    await page.waitForFunction(() => document.documentElement.lang === 'en')
    await page.getByRole('button', { name: 'Restart printer', exact: true }).waitFor()
    assert.ok(await page.getByRole('button', { name: 'Restart printer', exact: true }).isDisabled())
  }
  armed = true
  for (const page of pages) await page.reload()
  for (const page of pages) await page.getByRole('button', { name: 'Restart printer', exact: true }).waitFor({ state: 'visible' })
  const page = pages[0], restart = page.getByRole('button', { name: 'Restart printer', exact: true })
  await page.waitForFunction(() => !document.querySelector('button[aria-label="Restart printer"]').disabled)
  await restart.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel', exact: true }).click()
  assert.equal(posts, 0)
  await restart.click()
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click()
  await page.waitForFunction(() => document.querySelector('button[aria-label="Restart printer"]').disabled)
  assert.equal(posts, 1)
  await pages[1].reload()
  await pages[1].waitForFunction(() => document.querySelector('button[aria-label="Restart printer"]').disabled)
  await page.locator('#cc2-navigation a[href="#files"]').click()
  await page.getByRole('button', { name: 'part ü.gcode', exact: true }).click()
  for (const button of [page.locator('main button[aria-label="Download G-code"]'), page.getByRole('button', { name: 'Download G-code', exact: true }).last()]) {
    const waiting = page.waitForEvent('download')
    await button.click()
    const download = await waiting
    assert.equal(download.suggestedFilename(), 'part ü.gcode')
    const file = await download.path()
    assert.equal(await readFile(file, 'utf8'), 'G28\n')
    await rm(file)
  }
  console.log('PASS: reboot disabled/confirmation/shared pending state and both native G-code download buttons')
} finally {
  if (browser) await browser.close()
  process.kill('SIGTERM')
  await new Promise(resolve => process.exitCode === null ? process.once('exit', resolve) : resolve())
  await rm(temporary, { recursive: true, force: true })
}
