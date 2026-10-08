import { store } from './store'
import { post, request } from './api'

// Spool library kept by CC2 Control (/api/spools). Spools sit in the four Canvas trays (slots 0-3) or on
// the external spool holder (slot 4) and are charged with the filament the printer extrudes from them.
export const EXTERNAL = 4

export type Spool = {
  id: string
  name: string
  brand: string
  material: string
  color: string
  diameter: number
  density: number
  net: number
  remaining: number
  tare: number
  low: number
  price: number
  note: string
  created: number
  used: number
  archived: boolean
}
export type SpoolTray = { status: number; type: string; name: string; color: string; brand: string; code: string }
export type SpoolSlot = {
  slot: number
  spool: string
  last: string
  question: '' | 'inserted' | 'changed'
  question_since: number
  question_mm: number
  runout: boolean
  printer: SpoolTray | null
}
export type SpoolEvent = {
  time: number
  spool: string
  slot: number
  kind: 'print' | 'runout' | 'correction' | 'new'
  grams: number
  mm: number
  job: string
  result: string
}
export type SpoolLibrary = {
  available: boolean
  error: string
  enabled: boolean
  revision: number
  canvas: boolean
  active_tray: number
  slots: SpoolSlot[]
  job: { active: boolean; file: string; started: number; slots: { mm: number; grams: number; spool: string }[] }
  spools: Spool[]
  log: SpoolEvent[]
}
// What /api/printer carries so that every page notices an open question.
export type SpoolSummary = { enabled: boolean; revision: number; questions: number[] } | null | undefined

export const spools = store({ data: null as SpoolLibrary | null, ok: true })

export async function refreshSpools() {
  try {
    spools.set({ data: await request('/api/spools'), ok: true })
  } catch {
    spools.set({ ok: false })
  }
}

// The spool question dialog: a tray to answer for, opened by a question from the printer or by the user.
// `prefill` carries a material and colour just written to the tray, before the printer reports them.
export type ChooserReason = 'question' | 'material' | 'manual'
export const spoolChooser = store({
  slot: null as number | null,
  reason: 'manual' as ChooserReason,
  prefill: null as { material: string; color: string } | null,
})
export const openChooser = (
  slot: number,
  reason: ChooserReason = 'manual',
  prefill: { material: string; color: string } | null = null
) => spoolChooser.set({ slot, reason, prefill })

// Typical densities in g/cm3; the most specific pattern comes first. The form fills one in, the user can change it.
const DENSITIES: [RegExp, number][] = [
  [/^PLA.*CF/, 1.29],
  [/^PLA/, 1.24],
  [/^PETG.*CF/, 1.3],
  [/^(PETG|PCTG|CPE)/, 1.27],
  [/^PET/, 1.38],
  [/^ABS/, 1.04],
  [/^ASA/, 1.07],
  [/^(TPU|TPE)/, 1.21],
  [/^(PA.*CF|PAHT|PPA)/, 1.18],
  [/^(PA|NYLON)/, 1.14],
  [/^PC/, 1.2],
  [/^PVA/, 1.23],
  [/^HIPS/, 1.04],
  [/^PP/, 0.9],
  [/^BVOH/, 1.14],
]
export const densityFor = (material: string) =>
  DENSITIES.find(([pattern]) => pattern.test(material.trim().toUpperCase()))?.[1] ?? 1.24
export const MATERIALS = [
  'PLA',
  'PLA Matte',
  'PLA Silk',
  'PLA-CF',
  'PETG',
  'PETG-CF',
  'ABS',
  'ASA',
  'TPU',
  'PA',
  'PA-CF',
  'PC',
  'PVA',
  'HIPS',
]

export const gramsFor = (mm: number, diameter: number, density: number) =>
  (mm * Math.PI * diameter * diameter * density) / 4000
export const spoolGrams = (s: Spool, mm: number) => gramsFor(mm, s.diameter, s.density)

// Whole grams, with one decimal below 10 g.
export const g = (v: number) => (Math.abs(v) < 10 ? v.toFixed(1) : Math.round(v).toString())
export const percent = (s: Spool) => Math.max(0, Math.min(100, (s.remaining / Math.max(1, s.net)) * 100))
export const isLow = (s: Spool) => s.remaining <= Math.max(0, s.low)
export const spoolLabel = (s: Spool) => s.name || [s.brand, s.material].filter(Boolean).join(' ')
export const slotOf = (lib: SpoolLibrary, id: string) => lib.slots.find(x => x.spool === id)?.slot ?? -1
export const findSpool = (lib: SpoolLibrary | null, id: string) => (id && lib?.spools.find(s => s.id === id)) || null

// Whether a tray reports this spool's material and colour, as the backend judges it.
export const fits = (s: Spool, tray: { type: string; name?: string; color: string } | null) => {
  if (!tray) return false
  const m = s.material.toLowerCase()
  const material = !tray.type || m === tray.type.toLowerCase() || m === (tray.name || '').toLowerCase()
  return material && (!tray.color || s.color.toUpperCase() === tray.color.toUpperCase())
}

// Requests carry key=value lines (see spools.h).
const lines = (fields: Record<string, string | number | boolean | undefined>) =>
  Object.entries(fields)
    .filter(([, v]) => v !== undefined)
    .map(([k, v]) => `${k}=${typeof v === 'boolean' ? (v ? 1 : 0) : String(v).trim()}`)
    .join('\n')

export type SpoolFields = Partial<Record<keyof Spool | 'slot', string | number | boolean>>
export const saveSpool = (fields: SpoolFields) => post('/api/spools/save', lines(fields))
export const deleteSpool = (id: string) => post('/api/spools/delete', lines({ id }))
export const assignSpool = (slot: number, spool: string) => post('/api/spools/assign', lines({ slot, spool }))
export const dismissSlot = (slot: number) => post('/api/spools/dismiss', lines({ slot }))
export const adjustSpool = (id: string, weight: { remaining: number } | { gross: number }) =>
  post('/api/spools/adjust', lines({ id, ...weight }))
export const setTracking = (on: boolean) => post('/api/spools/enable', on ? 'on' : 'off')
