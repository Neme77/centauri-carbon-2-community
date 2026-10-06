import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser, machine = 2
let pid = { busy: false, heater: '', ready: false, failed: false, kp: null, ki: null, kd: null }
const actions = [], errors = []
try {
 await server.listen()
 browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox'] } : {}) })
 const page = await browser.newPage({ viewport: { width: 1366, height: 768 } })
 page.on('pageerror', e => errors.push(e.message))
 await page.route('**/api/preferences', r => r.fulfill({ json: { language: 'en', theme: 'dark' } }))
 await page.route('**/api/printer', r => r.fulfill({ json: { connected: true, last_message_age: 0, machine: { status: machine, status_name: machine === 1 ? 'Idle' : 'Printing' }, print: { state: machine === 1 ? 'idle' : 'printing' }, extruder: { temperature: 25 }, heater_bed: { temperature: 25 } } }))
 await page.route('**/api/pid', r => r.fulfill({ json: pid }))
 await page.route('**/api/control', r => { const a = r.request().postData(); actions.push(a); pid = { ...pid, ready: false, busy: true, heater: a.includes('heater_bed') ? 'heater_bed' : 'extruder' }; return r.fulfill({ status: 202, json: { accepted: true } }) })
 await page.goto(`${server.resolvedUrls.local[0]}#control`)
 const card = page.locator('.cc2-pid'), start = card.getByRole('button', { name: 'Calibrate Nozzle', exact: true }), save = card.getByRole('button', { name: 'Save PID and restart', exact: true })
 await card.waitFor(); assert.ok(await start.isDisabled()); assert.ok(await save.isDisabled())
 machine = 1; await start.waitFor(); await page.waitForFunction(() => !document.querySelector('.cc2-pid button').disabled)
 await card.getByRole('spinbutton', { name: 'Calibration temperature: Nozzle' }).fill('301'); await start.click(); assert.equal(actions.length, 0)
 await card.getByRole('spinbutton', { name: 'Calibration temperature: Nozzle' }).fill('200'); await start.click(); await page.getByRole('button', { name: 'Cancel', exact: true }).click(); assert.equal(actions.length, 0)
 await start.click(); await page.getByRole('button', { name: 'Confirm', exact: true }).click(); await page.waitForFunction(() => document.querySelector('.cc2-pid button').disabled); assert.deepEqual(actions, ['pid:extruder:200'])
 pid = { busy: false, heater: 'extruder', ready: true, failed: false, kp: 22.1, ki: 1.2, kd: 103.4 }
 await page.waitForFunction(() => [...document.querySelectorAll('.cc2-pid button')].some(b => b.textContent.includes('Save PID') && !b.disabled))
 assert.ok((await card.textContent()).includes('22.100'))
 await save.click(); await page.getByRole('button', { name: 'Cancel', exact: true }).click(); assert.equal(actions.length, 1)
 await save.click(); await page.getByRole('button', { name: 'Confirm', exact: true }).click(); assert.deepEqual(actions, ['pid:extruder:200', 'pid:save'])
 pid = { busy: false, heater: 'heater_bed', ready: false, failed: true, kp: null, ki: null, kd: null }
 await page.waitForFunction(() => document.querySelector('.cc2-pid').textContent.includes('Calibration failed'))
 assert.ok(await save.isDisabled())
 await page.setViewportSize({ width: 390, height: 844 }); await card.scrollIntoViewIfNeeded()
 assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), 'mobile must not overflow')
 assert.deepEqual(errors, [])
 console.log('PASS PID idle guards, temperature validation, confirm/cancel, command actions, readback, save gating and mobile layout')
} finally { await browser?.close(); await server.close() }
