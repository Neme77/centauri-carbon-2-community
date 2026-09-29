export const meshRoot = (d: any) => d?.result && (d.result.status?.bed_mesh || d.result.bed_mesh) || d?.status?.bed_mesh || null

export type Pt = { x: number; y: number; z: number }

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
  const matrix = root.probed_matrix || root.mesh_matrix || root.matrix
  if (!Array.isArray(matrix) || !matrix.length || !Array.isArray(matrix[0])) return null
  const min = Array.isArray(root.mesh_min) ? root.mesh_min : [0, 0]
  const max = Array.isArray(root.mesh_max) ? root.mesh_max : [matrix[0].length - 1, matrix.length - 1]
  const out: Pt[] = []
  matrix.forEach((row: any[], r: number) => {
    row.forEach((z, c) => {
      if (Number.isFinite(Number(z))) out.push({ x: min[0] + (max[0] - min[0]) * c / Math.max(1, row.length - 1), y: min[1] + (max[1] - min[1]) * r / Math.max(1, matrix.length - 1), z: Number(z) })
    })
  })
  return out
}
