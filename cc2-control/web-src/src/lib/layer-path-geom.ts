export type LayerPathPoint = [number, number]
export type LayerPathPolyline = LayerPathPoint[]
export type LayerSplitMode = 'printed' | 'remaining' | 'split'

function closestOnSegment(
  a: LayerPathPoint,
  b: LayerPathPoint,
  p: LayerPathPoint
): { t: number; q: LayerPathPoint; d2: number } {
  const abx = b[0] - a[0]
  const aby = b[1] - a[1]
  const len2 = abx * abx + aby * aby
  const t = len2 <= 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / len2))
  const q: LayerPathPoint = [a[0] + t * abx, a[1] + t * aby]
  const dx = q[0] - p[0]
  const dy = q[1] - p[1]
  return { t, q, d2: dx * dx + dy * dy }
}

/**
 * Split one layer's polylines at the point on the extrusion chain closest to the nozzle.
 * Progress is the print order of the paths, cut on the nearest segment (not only a vertex).
 */
export function splitLayerPaths(
  paths: LayerPathPolyline[],
  nozzle: [number, number] | null,
  mode: LayerSplitMode
): { printed: LayerPathPolyline[]; remaining: LayerPathPolyline[] } {
  if (mode === 'printed' || (mode === 'split' && !nozzle)) return { printed: paths, remaining: [] }
  if (mode === 'remaining' || !nozzle) return { printed: [], remaining: paths }

  let best: { pi: number; qi: number; t: number; q: LayerPathPoint; d2: number } | null = null
  for (let pi = 0; pi < paths.length; pi++) {
    const path = paths[pi]
    for (let qi = 0; qi < path.length - 1; qi++) {
      const hit = closestOnSegment(path[qi], path[qi + 1], nozzle)
      if (!best || hit.d2 < best.d2) best = { pi, qi, t: hit.t, q: hit.q, d2: hit.d2 }
    }
  }
  if (!best) return { printed: [], remaining: paths }
  const cut = best

  const printed: LayerPathPolyline[] = []
  const remaining: LayerPathPolyline[] = []
  const eps = 1e-6
  paths.forEach((path, pi) => {
    if (pi < cut.pi) printed.push(path)
    else if (pi > cut.pi) remaining.push(path)
    else {
      const { qi, t, q } = cut
      if (t <= eps) {
        if (qi > 0) printed.push(path.slice(0, qi + 1))
        remaining.push(path.slice(qi))
      } else if (t >= 1 - eps) {
        printed.push(path.slice(0, qi + 2))
        if (qi + 1 < path.length - 1) remaining.push(path.slice(qi + 1))
      } else {
        printed.push([...path.slice(0, qi + 1), q])
        remaining.push([q, ...path.slice(qi + 1)])
      }
    }
  })
  return { printed, remaining }
}
