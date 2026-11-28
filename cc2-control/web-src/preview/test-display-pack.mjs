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
  const page = await browser.newPage()
  await page.goto(server.resolvedUrls.local[0] + '#settings')
  const result = await page.evaluate(async () => {
    const mod = await import('/src/lib/display.ts')
    const canvas = document.createElement('canvas')
    canvas.width = 1200
    canvas.height = 800
    const context = canvas.getContext('2d')
    const gradient = context.createLinearGradient(0, 0, 1200, 800)
    gradient.addColorStop(0, '#123')
    gradient.addColorStop(0.5, '#c85')
    gradient.addColorStop(1, '#2a7')
    context.fillStyle = gradient
    context.fillRect(0, 0, 1200, 800)
    for (let i = 0; i < 40; i++) {
      context.fillStyle = `hsl(${i * 37},60%,${30 + (i % 40)}%)`
      context.beginPath()
      context.arc((i * 137) % 1200, (i * 211) % 800, 20 + (i % 90), 0, 7)
      context.fill()
    }
    const image = context.getImageData(0, 0, 1200, 800)
    for (let i = 0; i < image.data.length; i += 4) {
      const n = ((i * 7919) % 18) - 9
      image.data[i] += n
      image.data[i + 1] += n
      image.data[i + 2] += n
    }
    context.putImageData(image, 0, 0)
    const photo = await new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', 0.92))
    const file = new File([photo], 'photo.jpg', { type: 'image/jpeg' })

    const assets = []
    for (let i = 0; i < 50; i++) assets.push({ name: `accent${i}.png`, role: 'accent', width: 26, height: 26 })
    for (let i = 0; i < 4; i++) assets.push({ name: `bg${i}.png`, role: 'background', width: 532, height: 300 })
    for (let i = 0; i < 100; i++) assets.push({ name: `${i}.png`, role: 'boot', width: 532, height: 300 })

    /* The image module fetches each asset; serve tiny valid PNGs from a blob. */
    const pixel = document.createElement('canvas')
    pixel.width = 26
    pixel.height = 26
    pixel.getContext('2d').fillRect(0, 0, 26, 26)
    const pixelBlob = await new Promise(resolve => pixel.toBlob(resolve, 'image/png'))
    const originalFetch = window.fetch
    window.fetch = async () => new Response(await pixelBlob.arrayBuffer(), { status: 200 })

    const status = {
      supported: true, error: '', schema: 1, firmwareBase: 'test', themeDir: '/tmp',
      assets, fileCount: assets.length, customizedCount: 0, rebootRequired: false,
      phase: 0, recoveryRequired: false, backupAvailable: false, restoreAvailable: false,
      firmwareChanged: false, stateValid: true, experimental: true,
    }
    const pack = await mod.buildPack(status, {
      accent: '#7eead8', background: '#0a0c11', boot: 'custom', bootArt: file,
    })
    const packSize = pack.size

    /* A single accent image above the 512 KiB per-file limit is refused, and
     * a tar above the pack limit is refused as a whole. */
    const huge = new Uint8Array(mod.MAX_FILE_BYTES + 1)
    let hugeRefused = false
    try {
      mod.buildTar([{ name: 'big.png', bytes: new Uint8Array(mod.MAX_PACK_BYTES) }])
    } catch {
      hugeRefused = true
    }
    let accentRefused = false
    try {
      window.fetch = async () => new Response(huge, { status: 200 })
      /* Different names so the browser-side image cache cannot hide the fetch. */
      const fresh = { ...status, assets: status.assets.map(asset => ({ ...asset, name: 'huge-' + asset.name })) }
      await mod.buildPack(fresh, { accent: '#7eead8', background: '#0a0c11', boot: 'community', bootArt: null })
    } catch {
      accentRefused = true
    }
    window.fetch = originalFetch
    return {
      packSize,
      limit: mod.MAX_PACK_BYTES,
      tarBytes: mod.tarBytes([{ name: 'x.png', bytes: new Uint8Array(1000) }]),
      hugeRefused,
      accentRefused,
    }
  })
  assert(result.packSize <= result.limit, `pack ${result.packSize} > ${result.limit}`)
  assert(result.packSize > 0)
  assert.equal(result.tarBytes, 512 + 1024 + 1024)
  assert.equal(result.hugeRefused, true, 'buildTar must refuse a file above the pack limit')
  assert.equal(result.accentRefused, true, 'an oversized asset must be refused before upload')
  console.log(`PASS: a normal photo builds a ${(result.packSize / 1024 / 1024).toFixed(2)} MiB 100-frame pack (limit 4 MiB); oversized assets are refused before upload`)
} finally {
  if (browser) await browser.close()
  await server.close()
}
