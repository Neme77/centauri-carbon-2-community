import { request } from './api'
import { type LayerPathPoint, type LayerPathPolyline, splitLayerPaths } from './layer-path-geom'

export type { LayerPathPoint, LayerPathPolyline, LayerSplitMode } from './layer-path-geom'
export { splitLayerPaths }

export type LayerPath = {
  storage: string
  file: string
  layer: number
  total_layers: number
  z: number | null
  paths: LayerPathPolyline[]
  point_count: number
  truncated: boolean
}

const cache = new Map<string, LayerPath>()

export function layerPathUrl(storage: string, file: string, layer: number): string {
  const query = new URLSearchParams({ storage, file, layer: String(layer) })
  return `/api/gcode-files/layer-path?${query}`
}

export function layerPathCacheKey(storage: string, file: string, layer: number): string {
  return `${storage}\n${file}\n${layer}`
}

export const cachedLayerPath = (storage: string, file: string, layer: number) =>
  cache.get(layerPathCacheKey(storage, file, layer))

function isPoint(p: unknown): p is LayerPathPoint {
  return Array.isArray(p) && p.length === 2 && p.every(n => typeof n === 'number' && Number.isFinite(n))
}

function isLayerPath(data: unknown): data is LayerPath {
  if (!data || typeof data !== 'object') return false
  const row = data as LayerPath
  return (
    typeof row.layer === 'number' &&
    Array.isArray(row.paths) &&
    row.paths.every(path => Array.isArray(path) && path.every(isPoint))
  )
}

export const fetchLayerPath = async (storage: string, file: string, layer: number, signal?: AbortSignal) => {
  const key = layerPathCacheKey(storage, file, layer)
  const hit = cache.get(key)
  if (hit) return hit
  const data = await request(layerPathUrl(storage, file, layer), { signal })
  if (!isLayerPath(data)) throw Error('Layer path not available')
  if (!signal?.aborted) {
    cache.set(key, data)
    if (cache.size > 8) cache.delete(cache.keys().next().value as string)
  }
  return data
}
