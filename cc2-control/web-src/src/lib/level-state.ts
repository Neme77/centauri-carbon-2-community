import { request } from './api'
import { ls, store } from './store'
import { type BasisChoice, choiceValue, parseChoice } from './mesh-basis'
import { gridFromMeshReply, type MeshGrid } from './mesh-align'
import { findMeasure, type PlateLibrary, plates, refreshPlates, workMeasure } from './plates'

// State of the Bed alignment tab that other pages read too: the chosen basis of step 1 and its bed temperature, and
// which steps are folded. Step 1 keeps two choices: `choice`/`temp` with "With mesh" ticked (a saved or the temporary
// mesh, or the temperature of a new mesh), `plain`/`plainTemp` without it (only a material or a typed temperature).
const KEY = 'cc2-level-base'
type Saved = { choice: string; temp: string; plain: string; plainTemp: string }
const plainKind = (v: string) => ['material', 'custom'].includes(parseChoice(v).kind)
const read = (): Saved => {
  try {
    const v = JSON.parse(ls.get(KEY) || '{}')
    const choice = typeof v.choice === 'string' ? v.choice : ''
    const temp = typeof v.temp === 'string' ? v.temp : '60'
    // A choice saved before the two were kept apart: a temperature is taken over for the measurement without a mesh.
    const plain = typeof v.plain === 'string' ? v.plain : choice && plainKind(choice) ? choice : ''
    const plainTemp = typeof v.plainTemp === 'string' ? v.plainTemp : temp
    return { choice, temp, plain, plainTemp }
  } catch {
    return { choice: '', temp: '60', plain: '', plainTemp: '60' }
  }
}
export const levelBase = store<Saved>(read())
export const setLevelBase = (patch: Partial<Saved>) => {
  levelBase.set(patch)
  ls.set(KEY, JSON.stringify(levelBase.get()))
}
export const chooseBasis = (choice: BasisChoice, temp?: number | null) =>
  setLevelBase({ choice: choiceValue(choice), ...(temp == null ? {} : { temp: String(temp) }) })
// The temperature of a measurement without a mesh: a material or a typed temperature.
export const choosePlain = (choice: BasisChoice, temp?: number | null) =>
  setLevelBase({ plain: choiceValue(choice), ...(temp == null ? {} : { plainTemp: String(temp) }) })

// "With mesh" (step 1): the evaluation takes the plate shape of the chosen mesh off, with both ways of measuring. Kept
// in this browser; the earlier third way "With mesh" (cc2-level-mode 'mesh') becomes "Quick" with the tick.
const MESH_KEY = 'cc2-level-mesh'
export const LEVEL_MODE_KEY = 'cc2-level-mode'
const readMesh = () => {
  const kept = ls.get(MESH_KEY)
  if (kept === '1' || kept === '0') return kept === '1'
  if (ls.get(LEVEL_MODE_KEY) !== 'mesh') return false
  ls.set(LEVEL_MODE_KEY, 'quick')
  ls.set(MESH_KEY, '1')
  return true
}
export const levelMesh = store<{ on: boolean }>({ on: readMesh() })
export const setLevelMesh = (on: boolean) => {
  levelMesh.set({ on })
  ls.set(MESH_KEY, on ? '1' : '0')
}

// A mesh just saved becomes the basis: the library is read again first, so step 1 finds it. Used by the end of a
// calibration seen on the tab (useBasis).
export const adoptMesh = async (id: string) => {
  await refreshPlates()
  const fresh = findMeasure(plates.get().data, id)
  if (fresh) chooseBasis({ kind: 'mesh', id: fresh.measure.id }, fresh.measure.temp)
  return Boolean(fresh)
}

// The last temporary mesh of "Measure a new mesh": plate, bed temperature and when CC2 Control measured it. Kept in this
// browser, so the warning that the plate's stored mesh no longer fits the bed stays after a restart of CC2 Control,
// which forgets the working mesh itself. It is done once the plate has a measurement at that temperature from after
// it (the final mesh), or once the plate is gone.
const WORK_KEY = 'cc2-level-work'
export type WorkRef = { plate: string; temp: number; measured: number }
const readWork = (): WorkRef | null => {
  try {
    const v = JSON.parse(ls.get(WORK_KEY) || 'null')
    return v && typeof v.plate === 'string' && Number.isFinite(v.temp) && Number.isFinite(v.measured)
      ? { plate: v.plate, temp: Number(v.temp), measured: Number(v.measured) }
      : null
  } catch {
    return null
  }
}
export const levelWork = store<{ ref: WorkRef | null }>({ ref: readWork() })
const setWork = (ref: WorkRef | null) => {
  levelWork.set({ ref })
  if (ref) ls.set(WORK_KEY, JSON.stringify(ref))
  else ls.del(WORK_KEY)
}
// The stored measurement of the plate at that temperature that is newer than the working mesh: the final mesh.
export const workDone = (lib: PlateLibrary | null | undefined, ref: WorkRef) => {
  const plate = lib?.plates.find(p => p.id === ref.plate)
  return !plate || plate.measures.some(m => m.temp === ref.temp && m.measured >= ref.measured)
}
// Follows the library: a working mesh CC2 Control reports is remembered; one whose final mesh is measured is done.
export const noteWork = (lib: PlateLibrary | null | undefined) => {
  if (!lib) return
  const w = lib.calibration?.work_mesh
  const ref = levelWork.get().ref
  if (
    w &&
    lib.plates.some(p => p.id === w.plate) &&
    (ref?.plate !== w.plate || ref.temp !== w.temp || ref.measured !== w.measured)
  )
    setWork({ plate: w.plate, temp: w.temp, measured: w.measured })
  else if (ref && !w && workDone(lib, ref)) setWork(null)
}

// The final mesh of the remembered working mesh: the plate's newest stored measurement at that temperature from after it.
export const finalOfWork = (lib: PlateLibrary | null | undefined) => {
  const ref = levelWork.get().ref
  if (!ref) return null
  return (
    lib?.plates
      .find(p => p.id === ref.plate)
      ?.measures.filter(m => m.temp === ref.temp && m.measured >= ref.measured)
      .sort((a, b) => b.measured - a.measured)[0] ?? null
  )
}
// On every page (components/guided-chain: MeshStatus): the basis "working mesh" hands over to its final mesh once that
// is stored and the working mesh is gone, also when the Bed alignment tab is not open. Then the working mesh is noted.
export const followLibrary = (lib: PlateLibrary | null | undefined) => {
  if (!lib) return
  const job = lib.calibration
  if (parseChoice(levelBase.get().choice).kind === 'work' && !workMeasure(lib) && (!job || job.stage === 'off')) {
    const final = finalOfWork(lib)
    if (final) chooseBasis({ kind: 'mesh', id: final.id }, final.temp)
  }
  noteWork(lib)
}

// The working mesh just measured becomes the basis (the guided chain, also while the tab is not open).
export const adoptWork = async () => {
  await refreshPlates()
  const lib = plates.get().data
  noteWork(lib)
  const work = workMeasure(lib)
  if (work) chooseBasis({ kind: 'work' }, work.measure.temp)
  return Boolean(work)
}

// The mesh the printer applies now (GET /api/mesh: bed_mesh.probed_matrix between mesh_min and mesh_max), a basis of
// step 1 for whoever has no plate library. `read`: the printer has answered; only then does a saved choice of it count as
// gone when there is none.
export const activeMesh = store<{ grid: MeshGrid | null; read: boolean }>({ grid: null, read: false })
let activeAsked = false
export const refreshActiveMesh = async () => {
  if (activeAsked) return
  activeAsked = true
  try {
    activeMesh.set({ grid: gridFromMeshReply(await request('/api/mesh'), 'active'), read: true })
  } catch {
    // A failed request changes nothing (the printer may be restarting).
  } finally {
    activeAsked = false
  }
}

// The firmware unloads its active mesh during a screw measurement (measured on a CC2, 18.11.: GET /api/mesh reports an
// empty profile_name and probed_matrix from the preparation on, the stored profiles stay). So the mesh is copied when a
// measurement starts with the active mesh as the basis, and the evaluation uses that copy. Kept in this browser like the
// temporary mesh's reference, so it holds after another page or a reload; a new measurement replaces it with the mesh the
// printer applies then, if it applies one.
const ACTIVE_KEY = 'cc2-level-active'
export type ActiveCopy = { grid: MeshGrid; taken: number }
const readActiveCopy = (): ActiveCopy | null => {
  try {
    const v = JSON.parse(ls.get(ACTIVE_KEY) || 'null')
    const g = v?.grid
    // The same checks as a mesh of the printer (lib/mesh-align gridFromMeshReply).
    const grid = g
      ? gridFromMeshReply({
          result: {
            status: {
              bed_mesh: { probed_matrix: g.points, mesh_min: [g.min_x, g.min_y], mesh_max: [g.max_x, g.max_y] },
            },
          },
        })
      : null
    return grid && Number.isFinite(v.taken) ? { grid, taken: Number(v.taken) } : null
  } catch {
    return null
  }
}
export const levelActive = store<{ copy: ActiveCopy | null }>({ copy: readActiveCopy() })
// At the start of a measurement with the active mesh as the basis: the mesh the printer applies now becomes the copy.
// Without one (already unloaded by an earlier measurement) the copy stays.
export const takeActiveCopy = () => {
  const grid = activeMesh.get().grid
  if (!grid) return
  const copy = { grid, taken: Math.floor(Date.now() / 1000) }
  levelActive.set({ copy })
  ls.set(ACTIVE_KEY, JSON.stringify(copy))
}

// Folded steps: undefined follows the step's own default (open, or only open while it matters).
export const levelOpen = store<Record<string, boolean | undefined>>({})
