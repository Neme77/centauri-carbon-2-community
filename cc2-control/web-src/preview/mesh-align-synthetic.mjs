// Synthetic bed meshes for the mesh-guided screw alignment tests: a plane with a known tilt plus a known
// plate shape. Nothing here comes from a real printer.

// Deterministic noise in -1..1 (a fixed linear congruential sequence, so every run sees the same mesh).
export function noiseSource(seed = 12345) {
  let state = seed >>> 0
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0
    return state / 0x80000000 - 1
  }
}

// `tiltX` / `tiltY` are the height change over `span` mm; `shape(u, v)` gets coordinates scaled to -1..1
// over the mesh and returns millimetres. Row 0 is the smallest Y unless `row0IsMinY` is false.
export function syntheticMesh({
  cols = 11,
  rows = 11,
  min_x = 6,
  max_x = 246,
  min_y = 6,
  max_y = 246,
  base = 0,
  tiltX = 0,
  tiltY = 0,
  span = 240,
  shape = () => 0,
  row0IsMinY = true,
} = {}) {
  const points = Array.from({ length: rows }, (_, r) => {
    const fy = row0IsMinY ? r / (rows - 1) : 1 - r / (rows - 1)
    const y = min_y + (max_y - min_y) * fy
    return Array.from({ length: cols }, (_, c) => {
      const fx = c / (cols - 1)
      const x = min_x + (max_x - min_x) * fx
      return base + (tiltX / span) * x + (tiltY / span) * y + shape(fx * 2 - 1, fy * 2 - 1)
    })
  })
  return { points, min_x, max_x, min_y, max_y }
}

// Plate shapes with a peak-to-centre height of `mm`.
export const dome = mm => (u, v) => -mm * (u * u + v * v) / 2
export const bowl = mm => (u, v) => (mm * (u * u + v * v)) / 2
export const saddle = mm => (u, v) => (mm * (u * u - v * v)) / 2
export const twist = mm => (u, v) => mm * u * v

// The same mesh in the shape GET /api/mesh answers with, as a stored profile.
export const asProfileReply = (name, grid) => ({
  result: {
    status: {
      bed_mesh: {
        profile_name: '',
        mesh_min: [0, 0],
        mesh_max: [0, 0],
        probed_matrix: [],
        profiles: {
          [name]: {
            mesh_params: {
              min_x: grid.min_x,
              max_x: grid.max_x,
              min_y: grid.min_y,
              max_y: grid.max_y,
              x_count: grid.points[0].length,
              y_count: grid.points.length,
            },
            points: grid.points,
          },
        },
      },
    },
  },
})
