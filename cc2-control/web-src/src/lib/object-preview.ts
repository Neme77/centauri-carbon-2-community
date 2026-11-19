import { request } from './api'
import { store } from './store'

/** One drawn layer of an object: its height and the outer wall as flat x,y lists in bed millimetres. */
export type ObjectPreviewRing = { z: number; paths: number[][] }

export type ObjectPreview = {
  storage: string
  file: string
  object: string
  /** Layers of the object that have an outer wall. */
  layers: number
  /** Every step-th of those layers is drawn. */
  step: number
  z_max: number
  bounds: [number, number, number, number]
  rings: ObjectPreviewRing[]
  truncated: boolean
}

/** Why there is no preview: the file is over the size limit, the read ran into the time limit,
 *  the print is still starting, or the file has no such object / cannot be read. */
export type ObjectPreviewMissing = 'too_large' | 'limit' | 'starting' | 'missing'

const cache = new Map<string, ObjectPreview>()
const key = (storage: string, file: string, object: string) => `${storage}\n${file}\n${object}`

/** True while this page waits for a preview. The layer path shares the service's one scan slot with it,
 *  so its "busy" answers are expected for that long and must not count as a failure. */
export const objectPreviewLoading = store({ active: false })

export function objectPreviewUrl(storage: string, file: string, object: string): string {
  const query = new URLSearchParams({ storage, file, object })
  return `/api/gcode-files/object-preview?${query}`
}

export const cachedObjectPreview = (storage: string, file: string, object: string) =>
  cache.get(key(storage, file, object))

function isPreview(data: unknown): data is ObjectPreview {
  if (!data || typeof data !== 'object') return false
  const row = data as ObjectPreview
  return (
    typeof row.object === 'string' &&
    typeof row.z_max === 'number' &&
    Array.isArray(row.rings) &&
    row.rings.every(
      ring =>
        ring &&
        typeof ring.z === 'number' &&
        Array.isArray(ring.paths) &&
        ring.paths.every(
          path =>
            Array.isArray(path) && path.length % 2 === 0 && path.every(n => typeof n === 'number' && Number.isFinite(n))
        )
    )
  )
}

export const objectPreviewBusy = (error: unknown) => (error as { status?: number } | null)?.status === 503

export function objectPreviewMissing(error: unknown): ObjectPreviewMissing {
  const e = error as { status?: number; data?: { error?: unknown } } | null
  if (e?.status === 409) return 'starting'
  if (e?.status === 413) return e.data?.error === 'Object preview file too large' ? 'too_large' : 'limit'
  return 'missing'
}

export const fetchObjectPreview = async (storage: string, file: string, object: string, signal?: AbortSignal) => {
  const k = key(storage, file, object)
  const hit = cache.get(k)
  if (hit) return hit
  const data = await request(objectPreviewUrl(storage, file, object), { signal })
  if (!isPreview(data)) throw Error('Object preview not available')
  if (!signal?.aborted) {
    cache.set(k, data)
    // The service forgets the file's objects after 30 minutes without a request; this page keeps only a few.
    if (cache.size > 16) cache.delete(cache.keys().next().value as string)
  }
  return data
}

/** Forget what this page holds, for a new print or another file. */
export const clearObjectPreviews = () => cache.clear()

// The view of the approved pictures: from the front left, looking down at 30 degrees.
const COS30 = 0.866
export function projectPreviewPoint(x: number, y: number, z: number, cx: number, cy: number): [number, number] {
  return [(x - cx - (y - cy)) * COS30, -(x - cx + (y - cy)) * 0.5 - z]
}

/** The rings as SVG polylines in drawing order (lowest first), with the box they fill. */
export function objectPreviewDrawing(preview: ObjectPreview) {
  const [x0, y0, x1, y1] = preview.bounds
  const cx = (x0 + x1) / 2
  const cy = (y0 + y1) / 2
  let minX = Infinity
  let minY = Infinity
  let maxX = -Infinity
  let maxY = -Infinity
  const lines: { points: string; height: number }[] = []
  const zMax = preview.z_max > 0 ? preview.z_max : 1
  for (const ring of [...preview.rings].sort((a, b) => a.z - b.z)) {
    for (const path of ring.paths) {
      if (path.length < 4) continue
      const out: string[] = []
      for (let i = 0; i + 1 < path.length; i += 2) {
        const [px, py] = projectPreviewPoint(path[i], path[i + 1], ring.z, cx, cy)
        if (px < minX) minX = px
        if (px > maxX) maxX = px
        if (py < minY) minY = py
        if (py > maxY) maxY = py
        out.push(`${Math.round(px * 100) / 100},${Math.round(py * 100) / 100}`)
      }
      lines.push({ points: out.join(' '), height: Math.min(1, Math.max(0, ring.z / zMax)) })
    }
  }
  if (!lines.length) return null
  const pad = Math.max(1, (maxX - minX) * 0.04)
  return {
    lines,
    viewBox: `${minX - pad} ${minY - pad} ${maxX - minX + 2 * pad} ${maxY - minY + 2 * pad}`,
  }
}
