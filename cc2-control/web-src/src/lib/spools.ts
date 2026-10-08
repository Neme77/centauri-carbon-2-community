import { store } from './store'
import { post, request } from './api'
import type { Key } from './i18n'

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

// A material or one of its product lines: "PLA Matte", "PLA+" and "PLA-CF" are lines of "PLA".
export const isLineOf = (material: string, type: string) => {
  const m = material.toLowerCase(),
    base = type.toLowerCase()
  return base !== '' && m.startsWith(base) && ['', ' ', '-', '+'].includes(m.charAt(base.length))
}

// Whether a tray reports this spool's material and colour, as the backend judges it: a product line
// also fits a tray that only reports its base type.
export const fits = (s: Spool, tray: { type: string; name?: string; color: string } | null) => {
  if (!tray) return false
  const material =
    !tray.type || s.material.toLowerCase() === (tray.name || '').toLowerCase() || isLineOf(s.material, tray.type)
  return material && (!tray.color || s.color.toUpperCase() === tray.color.toUpperCase())
}

/* ---- the inventory hierarchy: manufacturer > kind > colour > spool ------------------------- */

const norm = (s: string) => s.trim().toLocaleLowerCase().replace(/ё/g, 'е')

// Hue in degrees (-1 for white, grey and black) and lightness of a #RRGGBB colour.
const hsl = (hex: string) => {
  const [r, g, b] = [1, 3, 5].map(i => Number.parseInt(hex.slice(i, i + 2), 16) / 255)
  const max = Math.max(r, g, b),
    min = Math.min(r, g, b),
    light = (max + min) / 2,
    d = max - min
  if (!Number.isFinite(d) || d < 0.08 || d / (1 - Math.abs(2 * light - 1) || 1) < 0.15) return { hue: -1, light }
  const h = max === r ? ((g - b) / d + 6) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4
  return { hue: h * 60, light }
}

// A plain colour word for a colour: names a spool from a tray, which reports only the colour value, and tells
// apart spools whose names hold no colour.
export const colourWord = (hex: string): Key => {
  const { hue: h, light } = hsl(hex)
  if (h < 0) return light > 0.85 ? 'spools.colour_white' : light < 0.2 ? 'spools.colour_black' : 'spools.colour_grey'
  if (h >= 15 && h < 45 && light < 0.4) return 'spools.colour_brown'
  if (h >= 20 && h < 60 && light > 0.75) return 'spools.colour_beige'
  if ((h >= 330 || h < 15) && light > 0.75) return 'spools.colour_pink'
  if (h < 15 || h >= 330) return 'spools.colour_red'
  if (h < 40) return 'spools.colour_orange'
  if (h < 70) return 'spools.colour_yellow'
  if (h < 165) return 'spools.colour_green'
  if (h < 195) return 'spools.colour_cyan'
  if (h < 250) return 'spools.colour_blue'
  if (h < 290 || light < 0.6) return 'spools.colour_purple'
  return 'spools.colour_pink'
}

// The colour part of a spool's name: the words that are neither its brand nor its material.
export const colourName = (s: Spool) => {
  const drop = new Set(`${s.brand} ${s.material}`.split(/\s+/).map(norm).filter(Boolean))
  return s.name
    .split(/\s+/)
    .filter(word => word && !drop.has(norm(word)))
    .join(' ')
}
// Still full: an unopened spool, so identical ones are interchangeable.
export const isSealed = (s: Spool) => s.remaining >= s.net - 1
// One colour row: the same brand, material and colour name, and colours that read alike. Colour values picked
// by hand for one colour differ a little; a name without a colour word ("PLA Matte" with material PLA) does
// not put red and blue spools on one row.
const colourKey = (s: Spool) => `${norm(colourName(s))}|${colourWord(s.color)}`
// Spools that differ only in how much is left: the same product, colour value and size.
export const twinKey = (s: Spool) =>
  `${norm(s.brand)}|${norm(s.material)}|${colourKey(s)}|${s.color.toUpperCase()}|${s.net}`

// Colour-wheel order: whites, greys and blacks first (light to dark), then by hue from red.
const wheel = (hex: string) => {
  const { hue, light } = hsl(hex)
  return hue < 0 ? [0, 1 - light] : [1, hue + (1 - light)]
}
const byWheel = (a: string, b: string) => {
  const [x, y] = [wheel(a), wheel(b)]
  return x[0] - y[0] || x[1] - y[1]
}

// `label`: the colour part of the spools' name, empty when it has none (the page shows the colour word).
export type SpoolStack = { key: string; label: string; color: string; spools: Spool[]; grams: number; low: boolean }
export type SpoolKind = { key: string; label: string; stacks: SpoolStack[]; count: number; grams: number }
// `other`: the brands with fewer than three spools; `label` is empty when no brand has three.
export type SpoolBrand = {
  key: string
  label: string
  other: boolean
  kinds: SpoolKind[]
  count: number
  grams: number
}

const total = (list: Spool[]) => list.reduce((sum, s) => sum + Math.max(0, s.remaining), 0)

const kindsOf = (list: Spool[], withBrand: boolean): SpoolKind[] => {
  const kinds = new Map<string, Spool[]>()
  for (const s of list) {
    const key = `${withBrand ? `${norm(s.brand)}|` : ''}${norm(s.material)}`
    kinds.set(key, [...(kinds.get(key) || []), s])
  }
  return [...kinds]
    .map(([key, spools]) => {
      const stacks = new Map<string, Spool[]>()
      for (const s of spools) stacks.set(colourKey(s), [...(stacks.get(colourKey(s)) || []), s])
      const first = spools[0]
      return {
        key,
        label: withBrand ? [first.brand, first.material].filter(Boolean).join(' ') : first.material,
        count: spools.length,
        grams: total(spools),
        stacks: [...stacks]
          .map(([colour, members]) => {
            // Opened spools first, emptiest first, so they are used up before a new one is opened.
            const ordered = [...members].sort(
              (a, b) => Number(isSealed(a)) - Number(isSealed(b)) || a.remaining - b.remaining
            )
            const grams = total(ordered)
            return {
              key: `${key}|${colour}`,
              label: colourName(ordered[0]),
              color: ordered[0].color,
              spools: ordered,
              grams,
              low: !ordered[0].archived && grams <= Math.max(...ordered.map(s => s.low)),
            }
          })
          .sort((a, b) => byWheel(a.color, b.color) || a.label.localeCompare(b.label)),
      }
    })
    .sort((a, b) => a.label.localeCompare(b.label))
}

// Manufacturers with three spools or more, the biggest first, then "Other" for the rest.
export function spoolHierarchy(list: Spool[]): SpoolBrand[] {
  const brands = new Map<string, Spool[]>()
  for (const s of list) brands.set(norm(s.brand), [...(brands.get(norm(s.brand)) || []), s])
  const main = [...brands].filter(([key, spools]) => key && spools.length >= 3)
  const rest = list.filter(s => !main.some(([key]) => key === norm(s.brand)))
  const out: SpoolBrand[] = main
    .sort((a, b) => b[1].length - a[1].length || a[0].localeCompare(b[0]))
    .map(([key, spools]) => ({
      key,
      label: spools[0].brand,
      other: false,
      kinds: kindsOf(spools, false),
      count: spools.length,
      grams: total(spools),
    }))
  if (rest.length)
    out.push({
      key: '',
      label: '',
      other: out.length > 0,
      kinds: kindsOf(rest, true),
      count: rest.length,
      grams: total(rest),
    })
  return out
}

export const matches = (s: Spool, query: string) =>
  !query.trim() || [s.name, s.brand, s.material, s.note, s.color].some(v => norm(v).includes(norm(query)))

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
