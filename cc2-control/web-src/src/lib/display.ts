import { request, errText } from '@/lib/api'
import { store } from '@/lib/store'
import { hexToRgb, hsl, hslToHex, normalizeHex, rgbToHex } from '@/lib/appearance'

/* Native touchscreen packs.
 *
 * The printer serves only the assets named by a firmware manifest
 * (/api/display). This module fetches those originals, applies bounded colour
 * or artwork transforms in the user's browser, packs the PNGs into an
 * uncompressed tar and uploads it as one request. The printer validates every
 * name, size, dimension and PNG chunk before touching a file and keeps a
 * baseline for restore. No layout, font, icon set or native program is
 * replaced. */

export type DisplayRole = 'boot' | 'background' | 'accent'

/* These mirror the server limits in cc2-control/src/display_theme.h
 * (DISP_MAX_PACK, DISP_MAX_FILE, DISP_MAX_ASSETS, DISP_MAX_STAGE). A test in
 * tests/test_display_limits_static.py fails if they drift. */
export const MAX_PACK_BYTES = 4 * 1024 * 1024
export const MAX_FILE_BYTES = 512 * 1024
export const MAX_ASSETS = 320
/* The native boot animation reads a fixed 100 frames. */
export const BOOT_FRAMES = 100
/* Leave room for the 1024-byte end marker and the tar headers already counted
 * by tarBytes(); a pack must never be built at exactly the server limit. */
const PACK_MARGIN_BYTES = 8 * 1024

export interface DisplayAsset {
  name: string
  role: DisplayRole
  width: number
  height: number
}

export interface DisplayStatus {
  supported: boolean
  error: string
  schema: number
  firmwareBase: string
  themeDir: string
  assets: DisplayAsset[]
  fileCount: number
  customizedCount: number
  rebootRequired: boolean
  phase: number
  recoveryRequired: boolean
  backupAvailable: boolean
  /* A verified baseline can be restored even when the current manifest or GUI
   * no longer matches it (firmware update). */
  restoreAvailable: boolean
  firmwareChanged: boolean
  stateValid: boolean
  experimental: boolean
}

export const display = store({
  status: null as DisplayStatus | null,
  loading: false,
  error: '',
  progress: 0,
  busy: false,
})

export async function loadDisplay() {
  display.set({ loading: true })
  try {
    const status = (await request('/api/display')) as DisplayStatus
    display.set({ status, loading: false, error: '' })
  } catch (error) {
    display.set({ loading: false, error: errText(error) })
  }
}

const assetUrl = (name: string) => `/api/display/asset?name=${encodeURIComponent(name)}`

/* A browser-side cache: the accent set is ~50 small images and is reused while
 * the user moves the colour picker. */
const cache = new Map<string, ImageData>()

const loadImage = async (name: string): Promise<ImageData> => {
  const hit = cache.get(name)
  if (hit) return hit
  const response = await fetch(assetUrl(name), { cache: 'no-store' })
  if (!response.ok) throw Error(`asset ${name}: HTTP ${response.status}`)
  const bitmap = await createImageBitmap(await response.blob())
  try {
    const canvas = document.createElement('canvas')
    canvas.width = bitmap.width
    canvas.height = bitmap.height
    const context = canvas.getContext('2d', { willReadFrequently: true })
    if (!context) throw Error('2D canvas unavailable')
    context.drawImage(bitmap, 0, 0)
    const data = context.getImageData(0, 0, bitmap.width, bitmap.height)
    cache.set(name, data)
    return data
  } finally {
    bitmap.close()
  }
}

const canvasFor = (data: ImageData) => {
  const canvas = document.createElement('canvas')
  canvas.width = data.width
  canvas.height = data.height
  const context = canvas.getContext('2d')
  if (!context) throw Error('2D canvas unavailable')
  context.putImageData(data, 0, 0)
  return canvas
}

const blobOf = (canvas: HTMLCanvasElement) =>
  new Promise<Blob>((resolve, reject) =>
    canvas.toBlob(blob => (blob ? resolve(blob) : reject(Error('PNG encoding failed'))), 'image/png')
  )

/* Recolour only the accent family of the authored interface art: hue 145-200°
 * with real saturation. Printer/Canvas illustrations, status green, warning
 * amber and red, filament colours and white text are outside that window and
 * stay byte-identical. Luminance is preserved so anti-aliased edges survive. */
export const recolorAccent = async (name: string, accent: string): Promise<Uint8Array> => {
  const source = await loadImage(name)
  const pixels = new Uint8ClampedArray(source.data)
  const target = hsl(normalizeHex(accent))
  for (let i = 0; i < pixels.length; i += 4) {
    if (pixels[i + 3] === 0) continue
    const [r, g, b] = [pixels[i], pixels[i + 1], pixels[i + 2]]
    const colour = hsl(rgbToHex(r, g, b))
    const hue = colour.h * 360
    if (hue < 145 || hue > 200 || colour.s < 0.22 || colour.l < 0.12) continue
    const [nr, ng, nb] = hexToRgb(hslToHex(target.h, Math.min(1, Math.max(0.35, colour.s * 1.05)), colour.l))
    pixels[i] = nr
    pixels[i + 1] = ng
    pixels[i + 2] = nb
  }
  return new Uint8Array(
    await (await blobOf(canvasFor(new ImageData(pixels, source.width, source.height)))).arrayBuffer()
  )
}

/* Tint a dark interface surface while keeping its relative luminance (and so
 * its separators and subtle gradients). The result is clamped to a dark
 * ceiling: native text is light and hard-coded, so a light background would
 * make the touchscreen unreadable. */
export const tintBackground = async (name: string, colour: string): Promise<Uint8Array> => {
  const source = await loadImage(name)
  const pixels = new Uint8ClampedArray(source.data)
  let [tr, tg, tb] = hexToRgb(normalizeHex(colour))
  /* Keep the chosen colour dark enough for the native light text. */
  const lum = (0.2126 * tr + 0.7152 * tg + 0.0722 * tb) / 255
  if (lum > 0.22) {
    const scale = 0.22 / Math.max(lum, 0.001)
    tr = Math.round(tr * scale)
    tg = Math.round(tg * scale)
    tb = Math.round(tb * scale)
  }
  for (let i = 0; i < pixels.length; i += 4) {
    if (pixels[i + 3] === 0) continue
    const l = (0.2126 * pixels[i] + 0.7152 * pixels[i + 1] + 0.0722 * pixels[i + 2]) / 255
    const factor = 0.32 + l * 0.95
    pixels[i] = Math.min(255, tr * factor)
    pixels[i + 1] = Math.min(255, tg * factor)
    pixels[i + 2] = Math.min(255, tb * factor)
  }
  return new Uint8Array(
    await (await blobOf(canvasFor(new ImageData(pixels, source.width, source.height)))).arrayBuffer()
  )
}

/* Turn an uploaded image into a small-palette boot frame no larger than
 * `maxBytes`. The native boot animation reads a fixed 100 frames; the same
 * frame is repeated, which is a static boot image rather than a new animation.
 * Quantisation is reduced until the encoded frame fits the per-frame budget,
 * so a normal photograph is accepted and the assembled pack stays inside the
 * server limit; only an image that cannot fit even at four levels is refused. */
export const bootFrame = async (
  file: File,
  background: string,
  width: number,
  height: number,
  maxBytes = MAX_FILE_BYTES
): Promise<Uint8Array> => {
  const bitmap = await createImageBitmap(file)
  try {
    const draw = document.createElement('canvas')
    draw.width = width
    draw.height = height
    const context = draw.getContext('2d', { willReadFrequently: true })
    if (!context) throw Error('2D canvas unavailable')
    context.fillStyle = normalizeHex(background)
    context.fillRect(0, 0, width, height)
    const scale = Math.min(width / bitmap.width, height / bitmap.height)
    const w = Math.max(1, Math.round(bitmap.width * scale))
    const h = Math.max(1, Math.round(bitmap.height * scale))
    context.imageSmoothingEnabled = true
    context.drawImage(bitmap, Math.round((width - w) / 2), Math.round((height - h) / 2), w, h)
    const image = context.getImageData(0, 0, width, height)
    const budget = Math.min(maxBytes, MAX_FILE_BYTES)
    for (const levels of [32, 16, 8, 4]) {
      const pixels = new Uint8ClampedArray(image.data)
      const step = 255 / (levels - 1)
      for (let i = 0; i < pixels.length; i += 4) {
        pixels[i] = Math.round(pixels[i] / step) * step
        pixels[i + 1] = Math.round(pixels[i + 1] / step) * step
        pixels[i + 2] = Math.round(pixels[i + 2] / step) * step
      }
      const blob = await blobOf(canvasFor(new ImageData(pixels, width, height)))
      if (blob.size <= budget) return new Uint8Array(await blob.arrayBuffer())
    }
    throw Error('Boot artwork is too detailed; choose a simpler image')
  } finally {
    bitmap.close()
  }
}

/* ---- tar -------------------------------------------------------------- */

export interface PackFile {
  name: string
  bytes: Uint8Array
}

const octal = (value: number, length: number) => `${value.toString(8).padStart(length - 1, '0')}\0`

/* Exact size of the tar that buildTar() will create: 512-byte header per file,
 * 512-byte padding per file and the 1024-byte end marker. */
export const tarBytes = (files: PackFile[]): number =>
  files.reduce((total, file) => total + 512 + Math.ceil(file.bytes.length / 512) * 512, 1024)

export const buildTar = (files: PackFile[]): Blob => {
  const size = tarBytes(files)
  if (size > MAX_PACK_BYTES) throw Error('Display pack is larger than the 4 MiB limit')
  const chunks: BlobPart[] = []
  for (const file of files) {
    const header = new Uint8Array(512)
    const encoder = new TextEncoder()
    const name = encoder.encode(file.name.slice(0, 99))
    header.set(name, 0)
    header.set(encoder.encode(octal(0o644, 8)), 100)
    header.set(encoder.encode(octal(0, 8)), 108)
    header.set(encoder.encode(octal(0, 8)), 116)
    header.set(encoder.encode(octal(file.bytes.length, 12)), 124)
    header.set(encoder.encode(octal(0, 12)), 136)
    header.fill(0x20, 148, 156)
    header[156] = 0x30 // regular file
    header.set(encoder.encode('ustar\0' + '00'), 257)
    let sum = 0
    for (const byte of header) sum += byte
    header.set(encoder.encode(octal(sum, 8)), 148)
    chunks.push(header, file.bytes as unknown as BlobPart)
    const padding = (512 - (file.bytes.length % 512)) % 512
    if (padding) chunks.push(new Uint8Array(padding))
  }
  chunks.push(new Uint8Array(1024))
  return new Blob(chunks, { type: 'application/x-tar' })
}

/* ---- build and upload ------------------------------------------------- */

export interface TouchscreenSelection {
  accent: string
  background: string
  boot: 'community' | 'custom'
  bootArt?: File | null
}

export const buildPack = async (
  status: DisplayStatus,
  selection: TouchscreenSelection,
  onStage?: (text: string, done: number, total: number) => void
): Promise<Blob> => {
  const files: PackFile[] = []
  const accents = status.assets.filter(a => a.role === 'accent')
  const backgrounds = status.assets.filter(a => a.role === 'background')
  const boots = status.assets.filter(a => a.role === 'boot')
  if (status.assets.length > MAX_ASSETS) throw Error('This firmware exposes more assets than the supported maximum')
  let done = 0
  const total = accents.length + backgrounds.length + (selection.boot === 'custom' ? 1 : 0)
  const stage = (text: string) => onStage?.(text, done, total)
  const add = (file: PackFile) => {
    if (file.bytes.length === 0 || file.bytes.length > MAX_FILE_BYTES)
      throw Error(`${file.name} is larger than the 512 KiB per-file limit`)
    files.push(file)
  }
  stage('accent')
  for (const asset of accents) {
    add({ name: asset.name, bytes: await recolorAccent(asset.name, selection.accent) })
    done++
    stage('accent')
  }
  stage('background')
  for (const asset of backgrounds) {
    add({ name: asset.name, bytes: await tintBackground(asset.name, selection.background) })
    done++
    stage('background')
  }
  if (selection.boot === 'custom') {
    stage('boot')
    if (!selection.bootArt) throw Error('Choose a boot image first')
    const frameAsset = boots[0]
    if (!frameAsset) throw Error('This firmware exposes no boot frames')
    if (!boots.length || boots.length > BOOT_FRAMES)
      throw Error('This firmware exposes an unexpected number of boot frames')
    /* Derive the per-frame budget from what the rest of the pack really
     * occupies, so 100 frames and the other assets always fit the server's
     * single 4 MiB request. */
    const others = tarBytes(files) - 1024
    const available = MAX_PACK_BYTES - PACK_MARGIN_BYTES - others - 1024
    const perFrame = Math.floor(available / boots.length) - 512 - 511
    if (perFrame < 1024) throw Error('The selected firmware assets leave no room for boot artwork')
    const frame = await bootFrame(
      selection.bootArt,
      selection.background,
      frameAsset.width,
      frameAsset.height,
      Math.min(perFrame, MAX_FILE_BYTES)
    )
    for (const asset of boots) add({ name: asset.name, bytes: frame })
    done++
    stage('boot')
  }
  stage('pack')
  if (files.length > MAX_ASSETS) throw Error('Display pack contains more files than supported')
  const size = tarBytes(files)
  if (size > MAX_PACK_BYTES - PACK_MARGIN_BYTES)
    throw Error('Display pack is larger than the 4 MiB limit; choose simpler boot artwork')
  return buildTar(files)
}

export const applyPack = (pack: Blob, onProgress?: (percent: number) => void): Promise<any> =>
  new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', '/api/display/apply')
    xhr.setRequestHeader('Content-Type', 'application/x-tar')
    xhr.setRequestHeader('X-CC2-Request', '1')
    xhr.timeout = 360000
    xhr.upload.onprogress = event => {
      if (event.lengthComputable) onProgress?.(Math.round((event.loaded / event.total) * 100))
    }
    xhr.onerror = () => reject(Error('Upload failed'))
    xhr.ontimeout = () => reject(Error('Upload timed out'))
    xhr.onload = () => {
      let body: any = null
      try {
        body = JSON.parse(xhr.responseText)
      } catch {
        /* keep null */
      }
      if (xhr.status >= 200 && xhr.status < 300 && body?.ok) resolve(body)
      else reject(Error(body?.error || `HTTP ${xhr.status}`))
    }
    xhr.send(pack)
  })

export const restoreDisplay = async () => {
  const response = await request('/api/display/restore', { method: 'POST', timeoutMs: 60000 })
  return response
}

export const reasonKey = (reason: string): string => {
  const known: Record<string, string> = {
    display_theme_missing: 'appearance.display_reason_theme_missing',
    display_theme_symlink: 'appearance.display_reason_unsafe',
    display_manifest_missing: 'appearance.display_reason_manifest_missing',
    display_manifest_invalid: 'appearance.display_reason_manifest_invalid',
    display_manifest_unreadable: 'appearance.display_reason_manifest_invalid',
    display_gui_missing: 'appearance.display_reason_gui',
    display_gui_unreadable: 'appearance.display_reason_gui',
    display_gui_mismatch: 'appearance.display_reason_gui_changed',
    display_active_theme_mismatch: 'appearance.display_reason_active_theme',
    display_active_theme_invalid: 'appearance.display_reason_active_theme',
    display_asset_modified: 'appearance.display_reason_modified',
    display_asset_foreign_change: 'appearance.display_reason_foreign',
    display_firmware_changed: 'appearance.display_reason_firmware_changed',
    display_backup_missing: 'appearance.display_reason_no_backup',
    display_asset_missing: 'appearance.display_reason_asset_missing',
    display_pack_invalid_header: 'appearance.display_reason_pack',
    display_pack_too_many_files: 'appearance.display_reason_pack',
    display_backup_mismatch: 'appearance.display_reason_backup',
    display_backup_write_failed: 'appearance.display_reason_backup',
    display_backup_manifest_failed: 'appearance.display_reason_backup',
    display_theme_changed: 'appearance.display_reason_theme_changed',
    display_restore_failed: 'appearance.display_reason_restore',
    display_state_invalid: 'appearance.display_reason_state',
    display_metadata_size_invalid: 'appearance.display_reason_state',
    display_metadata_unreadable: 'appearance.display_reason_state',
    display_metadata_truncated: 'appearance.display_reason_state',
    display_metadata_oom: 'appearance.display_reason_state',
    display_pack_truncated: 'appearance.display_reason_pack',
    display_pack_empty: 'appearance.display_reason_pack',
    display_pack_duplicate: 'appearance.display_reason_pack',
    display_pack_trailing_data: 'appearance.display_reason_pack',
    display_asset_not_allowed: 'appearance.display_reason_not_allowed',
    display_png_invalid: 'appearance.display_reason_png',
  }
  const key = reason.split(':')[0]
  return known[key] || 'appearance.display_reason_generic'
}
