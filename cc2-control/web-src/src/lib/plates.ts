import { store } from './store'
import { post, request } from './api'
import type { Pt } from './mesh'

// Build-plate library kept by CC2 Control (/api/plates): one entry per plate surface, with the meshes the
// printer measured on it at different bed temperatures, its Z offset and the list of nozzles. Side A prints
// with the printer's `default` mesh, Side B with `default1`; mounting a plate whose mesh is not in that slot
// restarts the printer to load it. A measurement stored in the printer as its own profile is loaded during
// the start of a print that chose it.
export type PlateMesh = {
  x_count: number
  y_count: number
  min_x: number
  max_x: number
  min_y: number
  max_y: number
  points: number[][]
}
export type Measure = {
  id: string
  temp: number
  nozzle: string
  measured: number
  // slot: the printer's side mesh is this one; profile: the printer keeps it as its own profile.
  slot: boolean
  profile: boolean
  mesh: PlateMesh
}
export type Plate = {
  id: string
  name: string
  side: 'A' | 'B'
  z_offset: number
  measure: string
  in_printer: boolean
  measures: Measure[]
}
export type Nozzle = { id: string; name: string; diameter: number; z_offset: number }
export type PrintMesh = {
  state: 'off' | 'waiting' | 'active'
  measure: string
  result: '' | 'loaded' | 'missed' | 'adaptive' | 'failed' | 'not_started' | 'ended'
}
// A bed mesh calibration CC2 Control runs: homing, heating, holding the bed at the temperature, probing and
// saving to a plate. `remaining` counts the soak in seconds; `bed` is the bed temperature now.
export type Calibration = {
  stage: 'off' | 'homing' | 'heating' | 'soaking' | 'probing' | 'saving'
  side: 'A' | 'B'
  temp: number
  soak: number
  remaining: number
  bed: number | null
  plate: string
  nozzle: string
  measure: string
  result: '' | 'done' | 'saved' | 'failed' | 'cancelled'
  error: '' | 'homing' | 'heating' | 'busy' | 'telemetry' | 'probing' | 'saving'
  detail: string
}
export type PlateLibrary = {
  available: boolean
  error: string
  current: string
  pending: string
  result: '' | 'rebooting' | 'mounted' | 'verify_failed' | 'reboot_failed'
  z_applied: boolean
  z_effective: number | null
  nozzle: string
  mesh_profile: string | null
  print_mesh: PrintMesh
  calibration: Calibration
  slots: { A: string; B: string }
  nozzles: Nozzle[]
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

// The Mesh tab can show a measurement instead of a printer profile: `measure:<id>`.
export const meshProfile = store({ profile: 'active' })

// The measurement a plate mounts with.
export const plateBase = (p: Plate) => p.measures.find(m => m.id === p.measure) || p.measures[0]

export const findMeasure = (lib: PlateLibrary | null | undefined, id: string) => {
  for (const plate of lib?.plates || []) {
    const measure = plate.measures.find(m => m.id === id)
    if (measure) return { plate, measure }
  }
  return null
}

export const nozzleName = (lib: PlateLibrary | null | undefined, id: string) =>
  lib?.nozzles.find(n => n.id === id)?.name || ''

// A print can use a measurement that is the side mesh or that the printer keeps as a profile.
export const usable = (m: Measure) => m.slot || m.profile

// The usable measurement nearest to a bed temperature; on a tie the same nozzle, the side mesh, then the newest.
export const nearestMeasure = (plate: Plate, temp: number | null, nozzle: string) => {
  const rank = (m: Measure) => [
    temp === null ? 0 : Math.abs(m.temp - temp),
    m.nozzle === nozzle ? 0 : 1,
    m.slot ? 0 : 1,
    -m.measured,
  ]
  const better = (a: Measure, b: Measure) => {
    const x = rank(a),
      y = rank(b)
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return x[i] < y[i]
    return false
  }
  return plate.measures.filter(usable).reduce<Measure | null>((best, m) => (!best || better(m, best) ? m : best), null)
}

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

// Whole degrees the backend accepts for a measurement.
export const tempOk = (text: string) => /^\d{2,3}$/.test(text.trim()) && Number(text) >= 40 && Number(text) <= 110

export const savePlate = (side: 'A' | 'B', name: string, z: number, temp: number, nozzle: string) =>
  post('/api/plates/save', `${side}\n${name}\n${z.toFixed(3)}\n${temp}\n${nozzle}`)
export const editPlate = (id: string, name: string, z: number) =>
  post('/api/plates/edit', `${id}\n${name}\n${z.toFixed(3)}`)
export const deletePlate = (id: string) => post('/api/plates/delete', id)
export const unmountPlate = () => post('/api/plates/unmount')
// `measure`: mount with that measurement instead of the one the plate mounted with last.
export const mountPlate = (id: string, reboot = false, measure = '') =>
  post('/api/plates/mount', [id, measure, reboot ? 'REBOOT' : ''].filter(Boolean).join('\n'))
// After a calibration on the plate: the side mesh becomes its measurement at `temp`.
export const measurePlate = (id: string, temp: number, nozzle: string) =>
  post('/api/plates/measure', `${id}\n${temp}\n${nozzle}`)
export const deleteMeasure = (id: string) => post('/api/plates/measure/delete', id)
// Corrects the temperature and nozzle a measurement records.
export const editMeasure = (id: string, temp: number, nozzle: string) =>
  post('/api/plates/measure/edit', `${id}\n${temp}\n${nozzle}`)
// Runs on the printer without the page: `plate` (or '') receives the result.
export const calibrate = (side: 'A' | 'B', temp: number, soak: number, nozzle: string, plate: string) =>
  post('/api/plates/calibrate', `${side}\n${temp}\n${soak}\n${nozzle}\n${plate}`)
export const stopCalibration = () => post('/api/plates/calibrate/cancel')
export const saveNozzle = (id: string, name: string, diameter: number, z: number) =>
  post('/api/plates/nozzle', `${id}\n${name}\n${diameter.toFixed(2)}\n${z.toFixed(3)}`)
export const deleteNozzle = (id: string) => post('/api/plates/nozzle/delete', id)
export const selectNozzle = (id: string) => post('/api/plates/nozzle/select', id)
