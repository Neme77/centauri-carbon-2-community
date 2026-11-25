export const meshRoot = (d: any) =>
  (d?.result && (d.result.status?.bed_mesh || d.result.bed_mesh)) || d?.status?.bed_mesh || null

export type Pt = { x: number; y: number; z: number }

// Bounds and probe heights of a saved mesh. Extra printer fields are ignored.
export type HeightMesh = {
  min_x: number
  max_x: number
  min_y: number
  max_y: number
  points: number[][]
}

// The same margin the printer uses when it decides two stored meshes are one mesh.
const boundsMatch = (a: number, b: number) => Math.abs(a - b) < 5e-7

// New mesh minus the previous one, probe by probe. Null when the grids differ: a pointwise
// change would then compare different places on the bed.
export function meshDelta(next: HeightMesh, prev: HeightMesh): Pt[] | null {
  if (
    next.points.length !== prev.points.length ||
    !boundsMatch(next.min_x, prev.min_x) ||
    !boundsMatch(next.max_x, prev.max_x) ||
    !boundsMatch(next.min_y, prev.min_y) ||
    !boundsMatch(next.max_y, prev.max_y)
  )
    return null
  const rows = next.points.length
  const out: Pt[] = []
  for (let r = 0; r < rows; r++) {
    const row = next.points[r]
    const old = prev.points[r]
    if (!row || !old || row.length !== old.length || row.length < 2) return null
    for (let c = 0; c < row.length; c++) {
      if (!Number.isFinite(row[c]) || !Number.isFinite(old[c])) return null
      out.push({
        x: next.min_x + ((next.max_x - next.min_x) * c) / Math.max(1, row.length - 1),
        y: next.min_y + ((next.max_y - next.min_y) * r) / Math.max(1, rows - 1),
        z: row[c] - old[c],
      })
    }
  }
  return out.length >= 4 ? out : null
}

export function matrixFromUds(data: any, profile = 'active'): Pt[] | null {
  const live = meshRoot(data)
  if (!live) return null
  let root = live
  if (profile !== 'active') {
    const p = live.profiles?.[profile]
    if (!p) return null
    const mp = p.mesh_params || {}
    root = { probed_matrix: p.points, mesh_min: [mp.min_x, mp.min_y], mesh_max: [mp.max_x, mp.max_y] }
  }
  const matrix = [root.probed_matrix, root.mesh_matrix, root.matrix].find(
    m => Array.isArray(m) && m.length && Array.isArray(m[0]) && m[0].length
  )
  if (!Array.isArray(matrix) || !matrix.length || !Array.isArray(matrix[0])) return null
  const min = Array.isArray(root.mesh_min) ? root.mesh_min : [0, 0]
  const max = Array.isArray(root.mesh_max) ? root.mesh_max : [matrix[0].length - 1, matrix.length - 1]
  const out: Pt[] = []
  matrix.forEach((row: any[], r: number) => {
    row.forEach((z, c) => {
      if (Number.isFinite(Number(z)))
        out.push({
          x: min[0] + ((max[0] - min[0]) * c) / Math.max(1, row.length - 1),
          y: min[1] + ((max[1] - min[1]) * r) / Math.max(1, matrix.length - 1),
          z: Number(z),
        })
    })
  })
  return out
}
