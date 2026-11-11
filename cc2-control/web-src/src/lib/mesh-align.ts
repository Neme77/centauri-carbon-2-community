// Mesh-guided screw alignment: what the four bed screws have to do, from a least-squares plane through
// every point of a bed mesh instead of four probes at the screws.
//
//   z = a + b·x + c·y   over all mesh points
//   tilt X = b · span, tilt Y = c · span          (what the bed is off by, over the configured span)
//   residual = measured - plane                   (the plate's own shape; the screws cannot change it)
//   correction at a screw = plane(reference) - plane(screw)
//
// Mesh heights follow the printer's bed mesh and the screw probe ("Result is z="): a larger z is a higher
// bed. A positive correction therefore means "raise the bed at this corner", a negative one "lower it".
// Nothing here talks to the printer and no measured mesh is built in: every number comes from the mesh
// and the configuration passed in. This file has no runtime imports, so the tests load it directly.

export type AlignScrewId = 'FL' | 'FR' | 'RR' | 'RL'
export type AlignScrew = { id: AlignScrewId; x: number; y: number }

// A mesh as the printer and the plate library report it: rows of heights between min and max.
export type MeshGrid = {
  points: number[][]
  min_x: number
  max_x: number
  min_y: number
  max_y: number
}

export type AlignConfig = {
  // Screw positions in bed coordinates (mm).
  screws: AlignScrew[]
  // The distance the tilt is stated over (mm).
  tilt_span_mm: number
  // Row 0 of the mesh is the smallest Y (the front of a CC2). False reads the rows back to front.
  row0_is_min_y: boolean
  // Both tilts must be below this for "aligned" (mm over the span).
  target_tilt_mm: number
  // The guided loop stops after this many measurements.
  max_rounds: number
  // A plate shape with a larger range gets a warning (mm). It never ends or blocks the alignment.
  shape_warn_mm: number
  // A plate shape with a smaller range is called nearly flat (mm).
  flat_range_mm: number
  // Turns are shown only when both the pitch and where it comes from are set.
  thread_pitch_mm: number | null
  thread_pitch_source: string
  // The turning direction is shown only when both the direction and where it comes from are set.
  clockwise_raises_bed: boolean | null
  rotation_source: string
}

// The CC2 standard, as defaults only. The three limits are estimates, not measured values.
export const ALIGN_DEFAULTS: AlignConfig = {
  screws: [
    { id: 'FL', x: 30, y: 30 },
    { id: 'FR', x: 230, y: 30 },
    { id: 'RR', x: 230, y: 225 },
    { id: 'RL', x: 30, y: 225 },
  ],
  tilt_span_mm: 240,
  row0_is_min_y: true,
  target_tilt_mm: 0.05,
  max_rounds: 4,
  shape_warn_mm: 0.3,
  flat_range_mm: 0.1,
  thread_pitch_mm: null,
  thread_pitch_source: '',
  clockwise_raises_bed: null,
  rotation_source: '',
}

export type AlignPoint = { x: number; y: number; z: number }
export type Plane = { a: number; b: number; c: number }
export type AlignShape = 'flat' | 'dome' | 'bowl' | 'saddle' | 'shape'
export type AlignDirection = 'cw' | 'ccw'

export type ScrewCorrection = AlignScrew & {
  // Height of the fitted plane at the screw (mm, in mesh coordinates).
  plane: number
  // Positive: raise the bed at this corner. Negative: lower it. Zero for the reference screw.
  correction: number
  reference: boolean
  // Null unless the thread pitch and its source are configured.
  turns: number | null
  // Null unless the turning direction and its source are configured, or nothing has to move.
  direction: AlignDirection | null
}

export type MeshAnalysis = {
  count: number
  plane: Plane
  // Height change of the plane over the span: positive X rises to the right, positive Y to the back.
  tilt_x: number
  tilt_y: number
  span: number
  // Range of the measured mesh, tilt included.
  raw_range: number
  // Measured minus plane, at the mesh positions, and what is left of the range once the tilt is gone.
  residual: AlignPoint[]
  residual_range: number
  residual_rms: number
  shape: AlignShape
  screws: ScrewCorrection[]
  reference: AlignScrewId
  // Every screw lies inside the measured area; outside it the plane is an extrapolation.
  covers_screws: boolean
  // Both tilts are below the configured target.
  aligned: boolean
  // The plate shape exceeds the warning range, whether or not the alignment is done.
  shape_warning: boolean
}

const finite = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v)

// The mesh as points in bed coordinates. Columns run from min_x to max_x; rows from min_y to max_y, or the
// other way round when row 0 is the back.
export function gridPoints(grid: MeshGrid, row0IsMinY = true): AlignPoint[] {
  const rows = Array.isArray(grid?.points) ? grid.points : []
  const out: AlignPoint[] = []
  rows.forEach((row, r) => {
    if (!Array.isArray(row)) return
    const fy = rows.length > 1 ? r / (rows.length - 1) : 0
    row.forEach((z, c) => {
      if (!finite(z)) return
      const fx = row.length > 1 ? c / (row.length - 1) : 0
      out.push({
        x: grid.min_x + (grid.max_x - grid.min_x) * fx,
        y: row0IsMinY ? grid.min_y + (grid.max_y - grid.min_y) * fy : grid.max_y - (grid.max_y - grid.min_y) * fy,
        z,
      })
    })
  })
  return out
}

// Least squares z = a + b·x + c·y. Null with fewer than three points or points on one line.
export function fitPlane(points: AlignPoint[]): Plane | null {
  const n = points.length
  if (n < 3) return null
  let mx = 0,
    my = 0,
    mz = 0
  for (const p of points) {
    mx += p.x
    my += p.y
    mz += p.z
  }
  mx /= n
  my /= n
  mz /= n
  let sxx = 0,
    sxy = 0,
    syy = 0,
    sxz = 0,
    syz = 0
  for (const p of points) {
    const dx = p.x - mx,
      dy = p.y - my,
      dz = p.z - mz
    sxx += dx * dx
    sxy += dx * dy
    syy += dy * dy
    sxz += dx * dz
    syz += dy * dz
  }
  const det = sxx * syy - sxy * sxy
  if (!(sxx > 0) || !(syy > 0) || !(det > 1e-9 * sxx * syy)) return null
  const b = (sxz * syy - syz * sxy) / det,
    c = (syz * sxx - sxz * sxy) / det
  return { a: mz - b * mx - c * my, b, c }
}

export const planeAt = (plane: Plane, x: number, y: number) => plane.a + plane.b * x + plane.c * y

// Solves the symmetric system m·q = v in place (Gauss with partial pivoting). Null when singular.
function solve(m: number[][], v: number[]): number[] | null {
  const n = v.length
  for (let i = 0; i < n; i++) {
    let pivot = i
    for (let r = i + 1; r < n; r++) if (Math.abs(m[r][i]) > Math.abs(m[pivot][i])) pivot = r
    if (Math.abs(m[pivot][i]) < 1e-12) return null
    ;[m[i], m[pivot]] = [m[pivot], m[i]]
    ;[v[i], v[pivot]] = [v[pivot], v[i]]
    for (let r = i + 1; r < n; r++) {
      const f = m[r][i] / m[i][i]
      for (let k = i; k < n; k++) m[r][k] -= f * m[i][k]
      v[r] -= f * v[i]
    }
  }
  const q = new Array<number>(n).fill(0)
  for (let i = n - 1; i >= 0; i--) {
    let s = v[i]
    for (let k = i + 1; k < n; k++) s -= m[i][k] * q[k]
    q[i] = s / m[i][i]
  }
  return q
}

// A quadratic surface that explains less of the residual than this is not named after its curvature.
const SHAPE_MIN_EXPLAINED = 0.5
// The weaker curvature must reach this share of the stronger one, with the other sign, for a saddle.
const SADDLE_MIN_RATIO = 0.4

// Names the plate shape from the residual alone. The residual is fitted with a quadratic surface over the
// measured area (coordinates scaled to -1..1 between the outermost measured points, so the edges of the
// mesh count as the edges, wherever on the bed the mesh ends). Its two principal curvatures decide:
// both down from the centre is a dome, both up a bowl, one each a saddle.
export function nameShape(residual: AlignPoint[], range: number, flatRange: number): AlignShape {
  if (!(range >= flatRange)) return 'flat'
  if (residual.length < 6) return 'shape'
  let x0 = Infinity,
    x1 = -Infinity,
    y0 = Infinity,
    y1 = -Infinity
  for (const p of residual) {
    x0 = Math.min(x0, p.x)
    x1 = Math.max(x1, p.x)
    y0 = Math.min(y0, p.y)
    y1 = Math.max(y1, p.y)
  }
  if (!(x1 > x0) || !(y1 > y0)) return 'shape'
  const cx = (x0 + x1) / 2,
    cy = (y0 + y1) / 2,
    hx = (x1 - x0) / 2,
    hy = (y1 - y0) / 2
  const basis = (p: AlignPoint) => {
    const u = (p.x - cx) / hx,
      v = (p.y - cy) / hy
    return [1, u, v, u * u, u * v, v * v]
  }
  const m = Array.from({ length: 6 }, () => new Array<number>(6).fill(0)),
    rhs = new Array<number>(6).fill(0)
  for (const p of residual) {
    const f = basis(p)
    for (let i = 0; i < 6; i++) {
      rhs[i] += f[i] * p.z
      for (let k = 0; k < 6; k++) m[i][k] += f[i] * f[k]
    }
  }
  const q = solve(m, rhs)
  if (!q) return 'shape'
  const mean = residual.reduce((s, p) => s + p.z, 0) / residual.length
  let total = 0,
    left = 0
  for (const p of residual) {
    const f = basis(p)
    let fit = 0
    for (let i = 0; i < 6; i++) fit += q[i] * f[i]
    total += (p.z - mean) ** 2
    left += (p.z - fit) ** 2
  }
  if (!(total > 0) || 1 - left / total < SHAPE_MIN_EXPLAINED) return 'shape'
  // Principal curvatures of q3·u² + q4·u·v + q5·v²: the height change from the centre to the edge.
  const half = (q[3] + q[5]) / 2,
    root = Math.hypot((q[3] - q[5]) / 2, q[4] / 2),
    k1 = half + root,
    k2 = half - root
  const strong = Math.abs(k1) >= Math.abs(k2) ? k1 : k2,
    weak = strong === k1 ? k2 : k1
  if (strong === 0) return 'shape'
  if (Math.sign(weak) === -Math.sign(strong) && Math.abs(weak) >= SADDLE_MIN_RATIO * Math.abs(strong)) return 'saddle'
  return strong < 0 ? 'dome' : 'bowl'
}

const TIE_MM = 1e-9

// The screw that needs the least total movement of the others: the smallest sum of absolute corrections,
// then the smallest largest correction, then the first in the list. Returns its index.
export function referenceScrew(planeHeights: number[]): number {
  let best = 0,
    bestSum = Infinity,
    bestMax = Infinity
  planeHeights.forEach((ref, i) => {
    let sum = 0,
      max = 0
    for (const h of planeHeights) {
      const c = Math.abs(ref - h)
      sum += c
      max = Math.max(max, c)
    }
    if (sum < bestSum - TIE_MM || (Math.abs(sum - bestSum) <= TIE_MM && max < bestMax - TIE_MM)) {
      best = i
      bestSum = sum
      bestMax = max
    }
  })
  return best
}

// Done only when both tilts are below the target; a tilt at the target is not below it.
export const tiltsBelowTarget = (tiltX: number, tiltY: number, target: number) =>
  Math.abs(tiltX) < target && Math.abs(tiltY) < target

// A correction smaller than this is shown as "nothing to do" (mm): it is below what the mesh resolves.
export const ALIGN_NOOP_MM = 0.005

const hasText = (s: unknown) => typeof s === 'string' && s.trim().length > 0

export const turnsConfigured = (config: AlignConfig) =>
  finite(config.thread_pitch_mm) && config.thread_pitch_mm > 0 && hasText(config.thread_pitch_source)
export const directionConfigured = (config: AlignConfig) =>
  typeof config.clockwise_raises_bed === 'boolean' && hasText(config.rotation_source)

export function analyseMesh(grid: MeshGrid, config: AlignConfig = ALIGN_DEFAULTS): MeshAnalysis | null {
  const points = gridPoints(grid, config.row0_is_min_y)
  const plane = fitPlane(points)
  if (!plane || !config.screws.length || !(config.tilt_span_mm > 0)) return null
  const residual = points.map(p => ({ x: p.x, y: p.y, z: p.z - planeAt(plane, p.x, p.y) }))
  const zs = points.map(p => p.z),
    rs = residual.map(p => p.z)
  const range = (v: number[]) => Math.max(...v) - Math.min(...v)
  const residualRange = range(rs)
  const heights = config.screws.map(s => planeAt(plane, s.x, s.y))
  const ref = referenceScrew(heights)
  const pitch = turnsConfigured(config) ? (config.thread_pitch_mm as number) : null
  const clockwiseRaises = directionConfigured(config) ? config.clockwise_raises_bed : null
  const screws: ScrewCorrection[] = config.screws.map((s, i) => {
    const correction = i === ref ? 0 : heights[ref] - heights[i]
    const moves = Math.abs(correction) >= ALIGN_NOOP_MM
    return {
      ...s,
      plane: heights[i],
      correction,
      reference: i === ref,
      turns: pitch === null ? null : Math.abs(correction) / pitch,
      direction: clockwiseRaises === null || !moves ? null : correction > 0 === clockwiseRaises ? 'cw' : 'ccw',
    }
  })
  const tiltX = plane.b * config.tilt_span_mm,
    tiltY = plane.c * config.tilt_span_mm
  const xs = points.map(p => p.x),
    ys = points.map(p => p.y)
  const x0 = Math.min(...xs),
    x1 = Math.max(...xs),
    y0 = Math.min(...ys),
    y1 = Math.max(...ys)
  return {
    count: points.length,
    plane,
    tilt_x: tiltX,
    tilt_y: tiltY,
    span: config.tilt_span_mm,
    raw_range: range(zs),
    residual,
    residual_range: residualRange,
    residual_rms: Math.sqrt(rs.reduce((s, r) => s + r * r, 0) / rs.length),
    shape: nameShape(residual, residualRange, config.flat_range_mm),
    screws,
    reference: config.screws[ref].id,
    covers_screws: config.screws.every(s => s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1),
    aligned: tiltsBelowTarget(tiltX, tiltY, config.target_tilt_mm),
    shape_warning: residualRange > config.shape_warn_mm,
  }
}

// A mesh of GET /api/mesh: 'active' is the mesh the printer applies now, any other name a stored profile
// (result.status.bed_mesh.profiles.<name>.points with its mesh_params).
export function gridFromMeshReply(data: any, profile = 'active'): MeshGrid | null {
  const root = data?.result?.status?.bed_mesh || data?.result?.bed_mesh || data?.status?.bed_mesh
  if (!root) return null
  const rect = (points: unknown, min: unknown, max: unknown): MeshGrid | null => {
    if (!Array.isArray(points) || points.length < 2 || !Array.isArray(points[0]) || points[0].length < 2) return null
    if (!Array.isArray(min) || !Array.isArray(max) || ![min[0], min[1], max[0], max[1]].every(finite)) return null
    if (!(max[0] > min[0]) || !(max[1] > min[1])) return null
    return { points: points as number[][], min_x: min[0], max_x: max[0], min_y: min[1], max_y: max[1] }
  }
  if (profile === 'active') return rect(root.probed_matrix, root.mesh_min, root.mesh_max)
  const p = root.profiles?.[profile],
    mp = p?.mesh_params
  return p && mp ? rect(p.points, [mp.min_x, mp.min_y], [mp.max_x, mp.max_y]) : null
}

// Settings kept in the browser. Anything missing or out of range falls back to the default.
export function parseAlignConfig(text: string | null): AlignConfig {
  let raw: any = null
  try {
    raw = text ? JSON.parse(text) : null
  } catch {
    raw = null
  }
  const d = ALIGN_DEFAULTS
  if (!raw || typeof raw !== 'object') return { ...d, screws: d.screws.map(s => ({ ...s })) }
  const within = (v: unknown, min: number, max: number, fallback: number) =>
    finite(v) && v >= min && v <= max ? v : fallback
  const screws = d.screws.map(s => {
    const own = Array.isArray(raw.screws) ? raw.screws.find((o: any) => o?.id === s.id) : null
    return { id: s.id, x: within(own?.x, 0, 400, s.x), y: within(own?.y, 0, 400, s.y) }
  })
  const pitch = finite(raw.thread_pitch_mm) && raw.thread_pitch_mm > 0 && raw.thread_pitch_mm <= 5
  return {
    screws,
    tilt_span_mm: within(raw.tilt_span_mm, 10, 1000, d.tilt_span_mm),
    row0_is_min_y: typeof raw.row0_is_min_y === 'boolean' ? raw.row0_is_min_y : d.row0_is_min_y,
    target_tilt_mm: within(raw.target_tilt_mm, 0.005, 2, d.target_tilt_mm),
    max_rounds: Math.round(within(raw.max_rounds, 1, 10, d.max_rounds)),
    shape_warn_mm: within(raw.shape_warn_mm, 0.01, 5, d.shape_warn_mm),
    flat_range_mm: within(raw.flat_range_mm, 0, 5, d.flat_range_mm),
    thread_pitch_mm: pitch ? raw.thread_pitch_mm : null,
    thread_pitch_source: typeof raw.thread_pitch_source === 'string' ? raw.thread_pitch_source.slice(0, 80) : '',
    clockwise_raises_bed: typeof raw.clockwise_raises_bed === 'boolean' ? raw.clockwise_raises_bed : null,
    rotation_source: typeof raw.rotation_source === 'string' ? raw.rotation_source.slice(0, 80) : '',
  }
}
