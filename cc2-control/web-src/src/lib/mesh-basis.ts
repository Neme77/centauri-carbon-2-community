// The basis of the Bed alignment tab (step 1): a saved mesh of a build plate, the temporary mesh of "Measure a new mesh" (kept
// by CC2 Control in memory only, see lib/plates workMeasure), the mesh the printer applies now (GET /api/mesh; its plate,
// temperature and nozzle are not known, so the bed temperature is chosen as without a mesh), or only a bed temperature.
// Whether a mesh is still usable as a basis: its age in days and the number of prints since it was measured.
// The limits are starting values, not measured ones (a mesh changes with the weeks and with the prints).

export const BASIS_MAX_AGE_DAYS = 30
export const BASIS_MAX_PRINTS = 10
export const BASIS_MAX_TEMP_C = 110
export const BASIS_MIN_TEMP_C = 30

const DAY_S = 86400

export type BasisChoice =
  | { kind: 'mesh'; id: string }
  | { kind: 'work' }
  | { kind: 'active' }
  | { kind: 'material'; name: string }
  | { kind: 'custom' }

// "measure:<id>", "work", "active", "material:<name>" or "custom": the value of the choice list.
export const choiceValue = (c: BasisChoice) =>
  c.kind === 'mesh'
    ? `measure:${c.id}`
    : c.kind === 'work' || c.kind === 'active'
      ? c.kind
      : c.kind === 'material'
        ? `material:${c.name}`
        : 'custom'
export const parseChoice = (v: unknown): BasisChoice => {
  const text = typeof v === 'string' ? v : ''
  if (text === 'work') return { kind: 'work' }
  if (text === 'active') return { kind: 'active' }
  if (text.startsWith('measure:') && text.length > 8) return { kind: 'mesh', id: text.slice(8) }
  if (text.startsWith('material:') && text.length > 9) return { kind: 'material', name: text.slice(9) }
  return { kind: 'custom' }
}

// Whole degrees from a text field: null unless it is a whole number from 30 to 110.
export const parseTemp = (text: string): number | null => {
  const s = text.trim()
  if (!/^\d{2,3}$/.test(s)) return null
  const n = Number(s)
  return n >= BASIS_MIN_TEMP_C && n <= BASIS_MAX_TEMP_C ? n : null
}

export type MeshAge = {
  // 'ok': usable; 'warn': over a limit; 'unknown': the mesh has no date.
  state: 'ok' | 'warn' | 'unknown'
  days: number | null
  // Null while the print list of the printer is not available.
  prints: number | null
  tooOld: boolean
  tooManyPrints: boolean
}

// Prints that began after the measurement. A deleted history counts too few; the printer's list is the only source.
export const printsSince = (tasks: { begin: number }[], measured: number) =>
  tasks.filter(x => x.begin > measured).length

export function meshAge(measured: number | null | undefined, nowS: number, tasks: { begin: number }[] | null): MeshAge {
  if (typeof measured !== 'number' || !Number.isFinite(measured) || measured <= 0)
    return { state: 'unknown', days: null, prints: null, tooOld: false, tooManyPrints: false }
  const days = Math.max(0, Math.floor((nowS - measured) / DAY_S))
  const prints = tasks ? printsSince(tasks, measured) : null
  const tooOld = days > BASIS_MAX_AGE_DAYS
  const tooManyPrints = prints !== null && prints > BASIS_MAX_PRINTS
  return { state: tooOld || tooManyPrints ? 'warn' : 'ok', days, prints, tooOld, tooManyPrints }
}
