import { store } from './store'
import { post, request } from './api'
import type { Pt } from './mesh'

// Build-plate library kept by CC2 Control (/api/plates): one entry per plate surface, with the mesh the
// printer measured for it and its Z offset. Side A prints with the printer's `default` mesh, Side B with
// `default1`; mounting a plate whose mesh is not in that slot restarts the printer to load it.
export type PlateMesh = {
  x_count: number
  y_count: number
  min_x: number
  max_x: number
  min_y: number
  max_y: number
  points: number[][]
}
export type Plate = {
  id: string
  name: string
  side: 'A' | 'B'
  z_offset: number
  measured: number
  in_printer: boolean
  mesh: PlateMesh
}
export type PlateLibrary = {
  available: boolean
  error: string
  current: string
  pending: string
  result: '' | 'rebooting' | 'mounted' | 'verify_failed' | 'reboot_failed'
  z_applied: boolean
  slots: { A: string; B: string }
  plates: Plate[]
}

// `rebooting`: a mount restarted the printer from this page; requests fail until it is back.
export const plates = store({ data: null as PlateLibrary | null, ok: true, rebooting: false })

export async function refreshPlates() {
  try {
    const data: PlateLibrary = await request('/api/plates')
    plates.set({ data, ok: true, rebooting: plates.get().rebooting && Boolean(data.pending) })
  } catch {
    plates.set({ ok: false })
  }
}

// The Mesh tab can show a plate instead of a printer profile: `plate:<id>`.
export const meshProfile = store({ profile: 'active' })

export const platePoints = (mesh: PlateMesh): Pt[] => {
  const out: Pt[] = []
  const rows = mesh.points.length
  mesh.points.forEach((row, r) => {
    row.forEach((z, c) => {
      out.push({
        x: mesh.min_x + ((mesh.max_x - mesh.min_x) * c) / Math.max(1, row.length - 1),
        y: mesh.min_y + ((mesh.max_y - mesh.min_y) * r) / Math.max(1, rows - 1),
        z,
      })
    })
  })
  return out
}

export const meshRange = (mesh: PlateMesh) => {
  const all = mesh.points.flat()
  return all.length ? Math.max(...all) - Math.min(...all) : 0
}

// Millimetres with an explicit sign, as the printer offsets are shown elsewhere.
export const zText = (z: number) => `${z > 0 ? '+' : z < 0 ? '−' : ''}${Math.abs(z).toFixed(3)}`

export const savePlate = (side: 'A' | 'B', name: string, z: number) =>
  post('/api/plates/save', `${side}\n${name}\n${z.toFixed(3)}`)
export const editPlate = (id: string, name: string, z: number) =>
  post('/api/plates/edit', `${id}\n${name}\n${z.toFixed(3)}`)
export const deletePlate = (id: string) => post('/api/plates/delete', id)
export const recapturePlate = (id: string) => post('/api/plates/recapture', id)
export const unmountPlate = () => post('/api/plates/unmount')
export const mountPlate = (id: string, reboot = false) => post('/api/plates/mount', reboot ? `${id}\nREBOOT` : id)
