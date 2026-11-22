import { errText } from '@/lib/api'
import { adoptMesh } from '@/lib/level-state'
import { type Calibration, measurePlate, type PlateLibrary } from '@/lib/plates'
import { ls, store } from '@/lib/store'

// A calibration on a plate that was measured and stands in the printer's side mesh, but could not be saved to the plate
// (result "failed", error "saving"; not a run that keeps only a working mesh). The note at the top of every page
// (components/guided-chain) stays until the plate has a newer measurement at that temperature and nozzle, a new
// calibration starts, or it is dismissed. The program does not date the end of the run, and the printer's clock is not
// this browser's, so "newer" is read from the library itself: the measurement the plate had when the failure was first
// seen is kept, and the note is over once the plate's measurement at that temperature and nozzle is a later one.
const KEY = 'cc2-mesh-unsaved'
export type UnsavedRef = { plate: string; temp: number; nozzle: string; before: number; dismissed: boolean }

const read = (): UnsavedRef | null => {
  try {
    const v = JSON.parse(ls.get(KEY) || 'null')
    return v &&
      typeof v.plate === 'string' &&
      Number.isFinite(v.temp) &&
      typeof v.nozzle === 'string' &&
      Number.isFinite(v.before)
      ? {
          plate: v.plate,
          temp: Number(v.temp),
          nozzle: v.nozzle,
          before: Number(v.before),
          dismissed: v.dismissed === true,
        }
      : null
  } catch {
    return null
  }
}
export const unsavedMesh = store<{ ref: UnsavedRef | null }>({ ref: read() })
const setRef = (ref: UnsavedRef | null) => {
  unsavedMesh.set({ ref })
  if (ref) ls.set(KEY, JSON.stringify(ref))
  else ls.del(KEY)
}

const failedSaving = (lib: PlateLibrary | null | undefined): Calibration | null => {
  const c = lib?.calibration
  return c && c.stage === 'off' && c.result === 'failed' && c.error === 'saving' && !c.work && c.plate ? c : null
}
const storedAt = (lib: PlateLibrary, plate: string, temp: number, nozzle: string) =>
  lib.plates.find(p => p.id === plate)?.measures.find(m => m.temp === temp && m.nozzle === nozzle)

// Called with every reading of the library: remembers a new failure, forgets the note once the run is no longer the last
// one (a new calibration started, or another result).
export const noteUnsavedMesh = (lib: PlateLibrary | null | undefined) => {
  if (!lib?.available) return
  const cur = unsavedMesh.get().ref
  const failed = failedSaving(lib)
  if (!failed) {
    if (cur) setRef(null)
    return
  }
  if (cur && cur.plate === failed.plate && cur.temp === failed.temp && cur.nozzle === failed.nozzle) return
  setRef({
    plate: failed.plate,
    temp: failed.temp,
    nozzle: failed.nozzle,
    before: storedAt(lib, failed.plate, failed.temp, failed.nozzle)?.measured ?? 0,
    dismissed: false,
  })
}

// The failed run the note is about; null when there is nothing to show.
export const unsavedRun = (lib: PlateLibrary | null | undefined, ref: UnsavedRef | null) => {
  const failed = failedSaving(lib)
  if (!lib || !ref || ref.dismissed || !failed) return null
  if (failed.plate !== ref.plate || failed.temp !== ref.temp || failed.nozzle !== ref.nozzle) return null
  const plate = lib.plates.find(p => p.id === ref.plate)
  if (!plate) return null
  const stored = storedAt(lib, ref.plate, ref.temp, ref.nozzle)
  if (stored && stored.measured > ref.before) return null
  return { plate, run: failed }
}

export const dismissUnsavedMesh = () => {
  const ref = unsavedMesh.get().ref
  if (ref) setRef({ ...ref, dismissed: true })
}

// Stores the printer's side mesh in the plate (the endpoint of the Build plates tab); the new measurement becomes the
// basis of Bed alignment. Returns the reason when the program refuses, null when it is stored.
export const saveUnsavedMesh = async (ref: UnsavedRef): Promise<string | null> => {
  try {
    const answer = await measurePlate(ref.plate, ref.temp, ref.nozzle)
    await adoptMesh(typeof answer?.measure === 'string' ? answer.measure : '')
    return null
  } catch (e) {
    return errText(e)
  }
}
