import assert from 'node:assert/strict'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { createServer } from 'vite'
import { chromium } from 'playwright'
const root = fileURLToPath(new URL('..', import.meta.url))
process.env.CC2_BACKEND = 'http://127.0.0.1:1'
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), mode: 'demo', server: { port: 0, host: '127.0.0.1' } })
let browser
try {
  await server.listen()
  const origin = server.resolvedUrls.local[0].replace(/\/$/, '')
  browser = await chromium.launch({ headless: true, ...(process.env.CC2_BROWSER_PATH ? { executablePath: process.env.CC2_BROWSER_PATH, args: ['--no-sandbox', '--disable-gpu', '--disable-software-rasterizer', '--single-process', '--no-zygote'] } : {}) })
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } })
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.addInitScript(() => {
    const p = CanvasRenderingContext2D.prototype
    const begin = p.beginPath, close = p.closePath, fill = p.fill, move = p.moveTo, line = p.lineTo, text = p.fillText
    p.beginPath = function(...args) { this.__closed = false; this.__vertices = []; return begin.apply(this, args) }
    p.closePath = function(...args) { this.__closed = true; return close.apply(this, args) }
    p.moveTo = function(x, y) { this.__vertices?.push([x, y]); return move.call(this, x, y) }
    p.lineTo = function(x, y) { this.__vertices?.push([x, y]); return line.call(this, x, y) }
    p.fillText = function(value, ...args) { (this.canvas.__labels ||= []).push(value); return text.call(this, value, ...args) }
    p.fill = function(...args) {
      const r = this.canvas.getBoundingClientRect()
      if (this.__closed && this.__vertices?.every(([x,y]) => x >= 0 && x <= r.width && y >= 0 && y <= r.height))
        this.canvas.__visibleFaces = (this.canvas.__visibleFaces || 0) + 1
      if (this.__closed) this.canvas.__faces = (this.canvas.__faces || 0) + 1
      return fill.apply(this, args)
    }
    const descriptor = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, 'width')
    Object.defineProperty(HTMLCanvasElement.prototype, 'width', { ...descriptor, set(value) { this.__faces = 0; this.__visibleFaces = 0; this.__labels = []; descriptor.set.call(this, value) } })
  })
  let rows = 4, cols = 4, flat = false, phase = 2075, requests = 0, offset = 0, spread = 1
  const matrix = (n, m, constant = false) => Array.from({ length: n }, (_, y) => Array.from({ length: m }, (_, x) => constant ? 0.06 + offset : 0.06 + offset + (x - y) * 0.005 * spread))
  const saved = { points: matrix(11, 11), mesh_params: { min_x: 6, min_y: 6, max_x: 246, max_y: 246 } }
  await page.route('**/api/preferences', route => route.fulfill({ json: { language: 'en', theme: 'dark' } }))
  await page.route('**/api/printer', async route => {
    const response = await route.fetch(), data = await response.json()
    data.machine.sub_status = phase; data.print.uuid = 'adaptive-test'
    await route.fulfill({ response, json: data })
  })
  await page.route('**/api/mesh', route => {
    requests++
    return route.fulfill({ json: { result: { status: { bed_mesh: {
      mesh_min: [99.8421, 117.468], mesh_max: [143.8521, 138.528], profile_name: 'ADAPTIVE',
      probed_matrix: matrix(rows, cols, flat),
      profiles: { default: saved, default1: saved, ADAPTIVE: { points: matrix(rows, cols, flat), mesh_params: { min_x: 99.8421, min_y: 117.468, max_x: 143.8521, max_y: 138.528 } } }
    } } } } })
  })
  await page.goto(`${origin}/#bed`)
  const canvas = page.locator('main canvas')
  const faces = async n => page.waitForFunction(n => document.querySelector('main canvas')?.__faces === n, n)
  await faces(9)
  const profile = page.locator('main select').first()
  await profile.selectOption('default'); await faces(100)
  await profile.selectOption('default1'); await faces(100)
  await profile.selectOption('ADAPTIVE'); await faces(9)
  await page.getByRole('button', { name: 'View 2D', exact: true }).click()
  await page.getByRole('button', { name: 'View 3D', exact: true }).click(); await faces(9)
  await profile.selectOption('active')
  const before = requests
  await page.waitForTimeout(2200)
  assert.equal(requests, before, 'unchanged printer phase must not repeatedly query the mesh')
  rows = 3; cols = 5; phase = 1045
  await faces(8)
  rows = 4; cols = 4; flat = true; phase = 2075
  await faces(9)
  assert.ok(requests > before, 'phase transition must refresh adaptive mesh without reloading the page')
  // Regression #75: verify the surface is visible, not just drawn off-screen.
  for (const [height, variation, constant] of [[2.064, 1, false], [-2.5, 1, false], [3, 1, true], [0, 100, false]]) {
    offset = height; spread = variation; flat = constant; phase++
    await page.waitForResponse(r => r.url().endsWith('/api/mesh'))
    const min = 0.06 + height - (constant ? 0 : 0.015 * variation)
    const label = `${min > 0 ? '+' : ''}${min.toFixed(3)} mm`
    await page.waitForFunction(label => {
      const c = document.querySelector('main canvas')
      return c?.__faces === 9 && c.__visibleFaces === 9 && c.__labels.includes(label)
    }, label)
  }
  await page.locator('#cc2-navigation button[aria-expanded]').click()
  await page.waitForFunction(() => {
    const c = document.querySelector('main canvas')
    return c.width === Math.round(c.getBoundingClientRect().width * Math.min(devicePixelRatio || 1, 2))
  })
  await page.setViewportSize({ width: 390, height: 844 }); await faces(9)
  assert.ok(await canvas.isVisible())
  assert.deepEqual(errors, [])
  console.log('PASS: adaptive 4x4/non-square/flat grids, saved A/B, 2D/3D, phase refresh and sidebar/mobile resize')
} finally { await browser?.close(); await server.close() }
