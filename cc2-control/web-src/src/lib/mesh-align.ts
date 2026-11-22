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
  // A plate shape with a larger range gets a warning (mm). It never ends or blocks the alignment.
  shape_warn_mm: number
  // A plate shape with a smaller range is called nearly flat (mm).
  flat_range_mm: number
  // Turns are shown only when both the pitch and where it comes from are set.
  thread_pitch_mm: number | null
  // The pitch the user has set in the experimental settings (mm per turn); null keeps the CC2 default.
  thread_pitch_user_mm?: number | null
  thread_pitch_source: string
  // The turning direction is shown only when both the direction and where it comes from are set.
  clockwise_raises_bed: boolean | null
  rotation_source: string
}

export const CC2_SCREW_SOURCE = 'CC2 M4 screw, measured from below'
export const USER_PITCH_SOURCE = 'set by the user'
// Limits of the pitch a user may set (mm per turn): M3 fine to M8 coarse.
export const PITCH_MIN_MM = 0.3
export const PITCH_MAX_MM = 1.5
// The CC2 standard, as defaults only. The three limits are estimates, not measured values.
export const ALIGN_DEFAULTS: AlignConfig = {
  screws: [
    { id: 'FL', x: 27.9, y: 25.95 },
    { id: 'FR', x: 227.15, y: 25.95 },
    { id: 'RR', x: 227.15, y: 225.5 },
    { id: 'RL', x: 27.9, y: 225.5 },
  ],
  tilt_span_mm: 240,
  row0_is_min_y: true,
  shape_warn_mm: 0.3,
  flat_range_mm: 0.1,
  // The CC2's own levelling screws: M4 (ISO coarse thread, 0.7 mm per turn), turned from below with a hex key.
  // Measured on a printer (four quarter/half turns at front-left): seen from below onto the screw
  // head, clockwise raises the corner. That is Klipper's CW-M4.
  thread_pitch_mm: 0.7,
  thread_pitch_source: CC2_SCREW_SOURCE,
  clockwise_raises_bed: true,
  rotation_source: CC2_SCREW_SOURCE,
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
  // The same amount as whole turns and clock-face minutes; null unless it is at least one minute and fewer than 100 turns.
  clock: ClockTurn | null
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
  // The plate shape exceeds the warning range.
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

// A correction smaller than this is shown as "nothing to do" (mm): it is below what the mesh resolves.
export const ALIGN_NOOP_MM = 0.005

const hasText = (s: unknown) => typeof s === 'string' && s.trim().length > 0

export const turnsConfigured = (config: AlignConfig) =>
  finite(config.thread_pitch_mm) && config.thread_pitch_mm > 0 && hasText(config.thread_pitch_source)
export const directionConfigured = (config: AlignConfig) =>
  typeof config.clockwise_raises_bed === 'boolean' && hasText(config.rotation_source)

// Clock-face turns, the notation of Klipper's SCREWS_TILT_CALCULATE: "CW 01:20" is one full turn and 20 minutes
// of a clock face (15 minutes is a quarter turn), clockwise. Format and rounding to whole minutes
// follow Klipper's screws_tilt_adjust (GPLv3); this is a re-implementation, no code was copied.
// Differences on purpose: a half minute always rounds up, an overflow of the minutes carries into the turns
// (Klipper can print "00:60"), less than one minute is "nothing to do", and 100 turns or more are not shown.
export type ClockTurn = { kind: 'turn'; turns: number; minutes: number }
export type ClockResult = ClockTurn | { kind: 'none' | 'mm' | 'invalid' }

// Turns that are not shown, and the half-minute guard against floating-point noise (59.4999 must not drop).
export const CLOCK_MAX_MINUTES = 100 * 60
const CLOCK_EPSILON = 1e-9

export function clockTurn(distanceMm: number, pitchMm: number): ClockResult {
  if (!finite(distanceMm) || !finite(pitchMm) || !(pitchMm > 0)) return { kind: 'invalid' }
  const total = Math.floor((Math.abs(distanceMm) / pitchMm) * 60 + 0.5 + CLOCK_EPSILON)
  if (total < 1) return { kind: 'none' }
  if (total >= CLOCK_MAX_MINUTES) return { kind: 'mm' }
  return { kind: 'turn', turns: Math.floor(total / 60), minutes: total % 60 }
}
const two = (n: number) => String(n).padStart(2, '0')
export const clockText = (c: ClockTurn) => `${two(c.turns)}:${two(c.minutes)}`
// "CW 01:20", or the amount alone when the direction is not known.
export const clockLabel = (c: ClockTurn, direction: AlignDirection | null) =>
  direction === null ? clockText(c) : `${direction === 'cw' ? 'CW' : 'CCW'} ${clockText(c)}`
// A positive move raises the bed at this corner; clockwise_raises_bed says which way that is turned.
export const turnDirection = (moveMm: number, clockwiseRaises: boolean): AlignDirection =>
  moveMm > 0 === clockwiseRaises ? 'cw' : 'ccw'

// Both tabs use this. moveMm is what the bed has to do at the corner, positive = raise. With a pitch set the
// result is the clock, or nothing when the move is under one minute or over 99 turns (then only millimetres
// are shown). Without a pitch only the direction can be shown.
export function screwClock(
  moveMm: number,
  config: AlignConfig
): { clock: ClockTurn | null; direction: AlignDirection | null } {
  const raises = directionConfigured(config) ? config.clockwise_raises_bed : null
  if (!turnsConfigured(config)) {
    const moves = finite(moveMm) && Math.abs(moveMm) >= ALIGN_NOOP_MM
    return { clock: null, direction: raises === null || !moves ? null : turnDirection(moveMm, raises) }
  }
  const r = clockTurn(moveMm, config.thread_pitch_mm as number)
  if (r.kind !== 'turn') return { clock: null, direction: null }
  return { clock: r, direction: raises === null ? null : turnDirection(moveMm, raises) }
}

// What each screw has to move so that all four reach the height of the reference screw.
function screwCorrections(config: AlignConfig, heights: number[], ref: number): ScrewCorrection[] {
  const pitch = turnsConfigured(config) ? (config.thread_pitch_mm as number) : null
  return config.screws.map((s, i) => {
    const correction = i === ref ? 0 : heights[ref] - heights[i]
    const moves = Math.abs(correction) >= ALIGN_NOOP_MM
    const shown = moves ? screwClock(correction, config) : { clock: null, direction: null }
    return {
      ...s,
      plane: heights[i],
      correction,
      reference: i === ref,
      turns: pitch === null ? null : Math.abs(correction) / pitch,
      clock: shown.clock,
      direction: shown.direction,
    }
  })
}

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
  const screws = screwCorrections(config, heights, ref)
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
    shape_warning: residualRange > config.shape_warn_mm,
  }
}

// Height of a mesh at a bed position, bilinear between the four points around it. Null outside the
// measured area or next to a missing point: nothing is extrapolated.
export function meshAt(grid: MeshGrid, x: number, y: number, row0IsMinY = true): number | null {
  const rows = Array.isArray(grid?.points) ? grid.points : []
  const width = Array.isArray(rows[0]) ? rows[0].length : 0
  if (rows.length < 2 || width < 2 || !(grid.max_x > grid.min_x) || !(grid.max_y > grid.min_y)) return null
  const u = ((x - grid.min_x) / (grid.max_x - grid.min_x)) * (width - 1)
  const fy = (y - grid.min_y) / (grid.max_y - grid.min_y)
  const v = (row0IsMinY ? fy : 1 - fy) * (rows.length - 1)
  if (!(u >= 0 && u <= width - 1 && v >= 0 && v <= rows.length - 1)) return null
  const c = Math.min(Math.floor(u), width - 2),
    r = Math.min(Math.floor(v), rows.length - 2)
  const z = [rows[r]?.[c], rows[r]?.[c + 1], rows[r + 1]?.[c], rows[r + 1]?.[c + 1]]
  if (!z.every(finite)) return null
  const tu = u - c,
    tv = v - r
  return (z[0] * (1 - tu) + z[1] * tu) * (1 - tv) + (z[2] * (1 - tu) + z[3] * tu) * tv
}

// The quick mode: four probes at the screws instead of a whole mesh. The plate shape comes from the
// last full mesh of the same plate (the reference) and is taken off each probe, so what is left is
// the tilt. A reference that does not fit the plate shows as a twist of the four points; a difference
// that looks like a tilt cannot be told from a real one, which is why the reference has to be the
// same plate at the same temperature. The limit is an estimate, not a measured value.
export const QUICK_TWIST_WARN_MM = 0.05

export type QuickAnalysis = {
  plane: Plane
  tilt_x: number
  tilt_y: number
  span: number
  screws: ScrewCorrection[]
  reference: AlignScrewId
  // The plate shape of the reference mesh at each screw, in the order of the configured screws.
  shape: number[]
  // Range of what the four probes leave once shape and tilt are gone.
  twist: number
  twist_warning: boolean
}

export function analyseScrewProbes(
  probes: Partial<Record<AlignScrewId, number>>,
  reference: MeshGrid,
  config: AlignConfig = ALIGN_DEFAULTS
): QuickAnalysis | null {
  const referencePlane = fitPlane(gridPoints(reference, config.row0_is_min_y))
  if (!referencePlane || config.screws.length < 3 || !(config.tilt_span_mm > 0)) return null
  const shape: number[] = [],
    points: AlignPoint[] = []
  for (const s of config.screws) {
    const z = probes[s.id],
      mesh = meshAt(reference, s.x, s.y, config.row0_is_min_y)
    if (!finite(z) || mesh === null) return null
    shape.push(mesh - planeAt(referencePlane, s.x, s.y))
    points.push({ x: s.x, y: s.y, z: z - shape[shape.length - 1] })
  }
  const plane = fitPlane(points)
  if (!plane) return null
  const heights = config.screws.map(s => planeAt(plane, s.x, s.y))
  const left = points.map((p, i) => p.z - heights[i])
  const twist = Math.max(...left) - Math.min(...left)
  const ref = referenceScrew(heights)
  const tiltX = plane.b * config.tilt_span_mm,
    tiltY = plane.c * config.tilt_span_mm
  return {
    plane,
    tilt_x: tiltX,
    tilt_y: tiltY,
    span: config.tilt_span_mm,
    screws: screwCorrections(config, heights, ref),
    reference: config.screws[ref].id,
    shape,
    twist,
    twist_warning: twist > QUICK_TWIST_WARN_MM,
  }
}

// The reference mesh carried along to where four screw probes say the plate is now: its shape (the mesh minus its
// own plane) on the surface through the probes with the shape taken off. With the screws on a rectangle that surface
// is bilinear between them (tilt and twist), otherwise the fitted plane. Calculated for the display, never measured.
export function carryMesh(
  reference: MeshGrid,
  probes: Partial<Record<AlignScrewId, number>>,
  config: AlignConfig = ALIGN_DEFAULTS
): MeshGrid | null {
  const fit = analyseScrewProbes(probes, reference, config)
  const referencePlane = fitPlane(gridPoints(reference, config.row0_is_min_y))
  if (!fit || !referencePlane) return null
  const at = (id: AlignScrewId) => {
    const i = config.screws.findIndex(s => s.id === id)
    const z = probes[id]
    return i < 0 || !finite(z) ? null : { ...config.screws[i], z: z - fit.shape[i] }
  }
  const fl = at('FL'),
    fr = at('FR'),
    rr = at('RR'),
    rl = at('RL')
  const same = (p: number, q: number) => Math.abs(p - q) < 0.01
  const rectangle =
    fl &&
    fr &&
    rr &&
    rl &&
    same(fl.x, rl.x) &&
    same(fr.x, rr.x) &&
    same(fl.y, fr.y) &&
    same(rl.y, rr.y) &&
    fr.x > fl.x &&
    rl.y > fl.y
  const rigid = (x: number, y: number) => {
    if (!rectangle) return planeAt(fit.plane, x, y)
    const u = (x - fl.x) / (fr.x - fl.x),
      v = (y - fl.y) / (rl.y - fl.y)
    return (fl.z * (1 - u) + fr.z * u) * (1 - v) + (rl.z * (1 - u) + rr.z * u) * v
  }
  const rows = reference.points.length
  return {
    ...reference,
    points: reference.points.map((row, r) =>
      row.map((z, c) => {
        const x = reference.min_x + ((reference.max_x - reference.min_x) * c) / Math.max(1, row.length - 1)
        const f = r / Math.max(1, rows - 1)
        const y = config.row0_is_min_y
          ? reference.min_y + (reference.max_y - reference.min_y) * f
          : reference.max_y - (reference.max_y - reference.min_y) * f
        return finite(z) ? z - planeAt(referencePlane, x, y) + rigid(x, y) : z
      })
    ),
  }
}

// A mesh of GET /api/mesh: 'active' is the mesh the printer applies now, any other name a stored profile
// (result.status.bed_mesh.profiles.<name>.points with its mesh_params).
export function gridFromMeshReply(data: any, profile = 'active'): MeshGrid | null {
  const root = data?.result?.status?.bed_mesh || data?.result?.bed_mesh || data?.status?.bed_mesh
  if (!root) return null
  const rect = (points: unknown, min: unknown, max: unknown): MeshGrid | null => {
    if (!Array.isArray(points) || points.length < 2 || !Array.isArray(points[0]) || points[0].length < 2) return null
    // A Klipper mesh is a rectangle: rows of different length would be stretched across the full width.
    if (points.some(r => Array.isArray(r) && r.length !== points[0].length)) return null
    if (!Array.isArray(min) || !Array.isArray(max) || ![min[0], min[1], max[0], max[1]].every(finite)) return null
    if (!(max[0] > min[0]) || !(max[1] > min[1])) return null
    return { points: points as number[][], min_x: min[0], max_x: max[0], min_y: min[1], max_y: max[1] }
  }
  if (profile === 'active') return rect(root.probed_matrix, root.mesh_min, root.mesh_max)
  const p = root.profiles?.[profile],
    mp = p?.mesh_params
  return p && mp ? rect(p.points, [mp.min_x, mp.min_y], [mp.max_x, mp.max_y]) : null
}

// Two meshes with the same area and the same heights: a stored mesh has 6 decimals (autosave.cfg), the printer keeps the
// unrounded values, so a difference up to half the last digit is the same mesh (as CC2 Control compares, plates.h).
export function sameGrid(a: MeshGrid, b: MeshGrid): boolean {
  const close = (p: unknown, q: unknown) => finite(p) && finite(q) && Math.abs(p - q) <= 5e-7 + 1e-9
  if (!(['min_x', 'max_x', 'min_y', 'max_y'] as const).every(k => close(a[k], b[k]))) return false
  if (a.points.length !== b.points.length) return false
  return a.points.every(
    (row, r) =>
      Array.isArray(row) && row.length === b.points[r]?.length && row.every((z, c) => close(z, b.points[r][c]))
  )
}

// Every configured screw lies inside the measured area (an adaptive mesh of a print may cover only part of the bed).
export const gridCoversScrews = (grid: MeshGrid, config: AlignConfig = ALIGN_DEFAULTS) =>
  config.screws.every(s => meshAt(grid, s.x, s.y, config.row0_is_min_y) !== null)

// The default screw positions of the earlier version (30/30, 230/30, 230/225, 30/225), which lay 2 to 5 mm beside the
// screw centres. It stored them with every save of the settings; a stored copy of exactly these four stands for the
// defaults, so the measured centres apply. Positions set by the user are kept.
const EARLIER_DEFAULT_SCREWS: Record<AlignScrewId, [number, number]> = {
  FL: [30, 30],
  FR: [230, 30],
  RR: [230, 225],
  RL: [30, 225],
}
const earlierDefaults = (list: unknown) =>
  Array.isArray(list) &&
  (Object.keys(EARLIER_DEFAULT_SCREWS) as AlignScrewId[]).every(id => {
    const own = list.find((o: any) => o?.id === id)
    return own?.x === EARLIER_DEFAULT_SCREWS[id][0] && own?.y === EARLIER_DEFAULT_SCREWS[id][1]
  })

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
  const stored = earlierDefaults(raw.screws) ? null : raw.screws
  const screws = d.screws.map(s => {
    const own = Array.isArray(stored) ? stored.find((o: any) => o?.id === s.id) : null
    return { id: s.id, x: within(own?.x, 0, 400, s.x), y: within(own?.y, 0, 400, s.y) }
  })
  const userPitch =
    finite(raw.thread_pitch_user_mm) &&
    raw.thread_pitch_user_mm >= PITCH_MIN_MM &&
    raw.thread_pitch_user_mm <= PITCH_MAX_MM
      ? raw.thread_pitch_user_mm
      : null
  return {
    screws,
    tilt_span_mm: within(raw.tilt_span_mm, 10, 1000, d.tilt_span_mm),
    row0_is_min_y: typeof raw.row0_is_min_y === 'boolean' ? raw.row0_is_min_y : d.row0_is_min_y,
    shape_warn_mm: within(raw.shape_warn_mm, 0.01, 5, d.shape_warn_mm),
    flat_range_mm: within(raw.flat_range_mm, 0, 5, d.flat_range_mm),
    // The CC2's own screw is the default; stored values of older versions never change these four. Only the pitch
    // the user sets in the experimental settings (its own field) replaces the default pitch.
    thread_pitch_user_mm: userPitch,
    thread_pitch_mm: userPitch ?? d.thread_pitch_mm,
    thread_pitch_source: userPitch == null ? d.thread_pitch_source : USER_PITCH_SOURCE,
    clockwise_raises_bed: d.clockwise_raises_bed,
    rotation_source: d.rotation_source,
  }
}
