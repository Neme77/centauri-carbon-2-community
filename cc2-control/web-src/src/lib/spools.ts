import { store } from './store'
import { errText, post, request } from './api'
import { type Key, t, tpl } from './i18n'

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
  spoolman_id: string
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
  spoolman_enabled: boolean
  spoolman_url: string
  spoolman_has_auth: boolean
  // Usage that was not sent to Spoolman and waits for the user's decision (spoolman.h), and how many spools are
  // still linked to numbers of the old server after an address change.
  spoolman_held?: HeldEntry[]
  spoolman_stale_links?: number
  // Changes with every change of the Spoolman address or login (and after every start of the printer program): what was
  // read from Spoolman and what was chosen from it is only valid under the generation it was read under.
  spoolman_gen?: number
}
export type HeldReason = 'login_changed' | 'server_changed' | 'not_found' | 'unsure'
export type HeldEntry = {
  id: number
  spool: string
  name: string
  spoolman_id: string
  grams: number
  time: number
  reason: HeldReason
  sending: boolean
}
export type SpoolmanCatalog = {
  gen?: number // the generation of the server settings the list was read under (SpoolLibrary.spoolman_gen)
  loading: boolean
  error: string
  spools: { id: string; name: string; material: string; color: string; remaining: number }[]
  truncated?: boolean // Spoolman has more spools than the list holds
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
export const spoolmanLabel = (s: Pick<Spool, 'spoolman_id'>) =>
  s.spoolman_id ? tpl('spools.spoolman_id_value', { id: s.spoolman_id }) : ''
export const spoolChoiceLabel = (s: Spool) => [spoolLabel(s), spoolmanLabel(s)].filter(Boolean).join(' · ')
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

// A tray that holds filament but reports neither type nor colour (the printer's exclamation mark). `fits` takes
// "nothing reported" as a match, which is right for "does the question need asking" but not for "is it worth writing".
export const reportsNothing = (tray: { type: string; color: string } | null) =>
  Boolean(tray && !tray.type && !tray.color)

// Whether the spool's filament should be written to the tray: it differs from what the tray reports, or the tray
// reports nothing at all. The question whether to ask which spool it is keeps using `fits` alone.
export const needsWrite = (s: Spool, tray: { type: string; name?: string; color: string } | null) =>
  !fits(s, tray) || reportsNothing(tray)

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
// Linked physical spools remain distinct even when their product, colour and size match.
export const twinKey = (s: Spool) =>
  `${norm(s.brand)}|${norm(s.material)}|${colourKey(s)}|${s.color.toUpperCase()}|${s.net}|${s.spoolman_id || ''}`

// Colour-wheel order: whites, greys and blacks first (light to dark), then by hue from red.
const wheel = (hex: string) => {
  const { hue, light } = hsl(hex)
  return hue < 0 ? [0, 1 - light] : [1, hue + (1 - light)]
}
export const byWheel = (a: string, b: string) => {
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
  !query.trim() ||
  [
    s.name,
    s.brand,
    s.material,
    s.note,
    s.color,
    s.spoolman_id,
    s.spoolman_id ? `#${s.spoolman_id}` : '',
    spoolmanLabel(s),
  ].some(v => norm(v || '').includes(norm(query)))

// The product line a tray names, from its base type on, without the maker's own word before it: Elegoo's Rapid
// PLA+ spools name "RAPID PLA+", which is the line "PLA+" of "PLA". A name in which no word starts a line of the
// type is kept whole.
export const lineCore = (name: string, type: string) => {
  const words = name.trim().split(/\s+/)
  const at = words.findIndex((_, i) => isLineOf(words.slice(i).join(' '), type))
  return at < 0 ? name : words.slice(at).join(' ')
}

// Whether a spool could be the filament a tray reports: its maker, type and colour as chosen on the printer.
// A tray that names a product line ("PLA Matte") takes that line or the plain type, also the line without the
// maker's word before it ("RAPID PLA+" takes "PLA+"); one that names only its type ("PLA") takes any line of
// it. A named maker takes its own spools and those with no maker recorded; "Generic" names none. Colours need
// only read alike, as a colour picked by hand rarely matches the tray's value exactly.
export const suits = (s: Spool, tray: { type: string; name?: string; color: string; brand?: string } | null) => {
  if (!tray) return true
  const m = s.material.trim().toLowerCase(),
    type = tray.type.trim().toLowerCase(),
    line = (tray.name || '').trim().toLowerCase(),
    maker = (tray.brand || '').trim()
  const material =
    !type ||
    (line && line !== type ? m === line || m === type || m === lineCore(line, type) : isLineOf(s.material, tray.type))
  const colour =
    !tray.color || s.color.toUpperCase() === tray.color.toUpperCase() || colourWord(s.color) === colourWord(tray.color)
  const brand = !maker || /^generic$/i.test(maker) || !s.brand.trim() || norm(s.brand) === norm(maker)
  return Boolean(material && colour && brand)
}

// The likeliest answers first: the tray's very colour, its product line as named, then without the maker's
// word, and its brand, then opened spools, the emptiest first, as they are used up before a new one is opened.
export const bySuit = (tray: { type: string; name?: string; color: string; brand?: string }) => {
  const line = (tray.name || tray.type).trim().toLowerCase(),
    core = lineCore(line, tray.type.trim())
  const rank = (s: Spool) => {
    const m = s.material.trim().toLowerCase()
    return [
      s.color.toUpperCase() === tray.color.toUpperCase() ? 0 : 1,
      m === line ? 0 : m === core ? 1 : 2,
      tray.brand && !/^generic$/i.test(tray.brand) && norm(s.brand) === norm(tray.brand) ? 0 : 1,
      isSealed(s) ? 1 : 0,
      s.remaining,
    ]
  }
  return (a: Spool, b: Spool) => {
    const [x, y] = [rank(a), rank(b)]
    return x.reduce((d, v, i) => d || v - y[i], 0)
  }
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
export const saveSpoolmanConfig = (fields: { enabled?: boolean; url?: string; basic_auth?: string }) =>
  post('/api/spoolman', lines(fields))

// The list of usage that was not sent to Spoolman. Nothing here runs by itself: every call is a button.
// `gen` is the generation the spools were chosen under: the printer refuses the request (409) when the address or the login
// changed since, instead of reading the numbers against another server.
export const sendHeld = (picks: Record<string, string>, gen: number | undefined) =>
  post('/api/spoolman/held/send', `gen=${gen ?? ''}\n${lines(picks)}`)
export const discardHeld = (id: number) => post('/api/spoolman/held/discard', lines({ id }))
export const unlinkOldConnections = () => post('/api/spoolman/unlink-old')
export const keepOldConnections = () => post('/api/spoolman/keep-old')
// Spoolman's own spools, for the choice next to each entry: asks the server to read them, then waits for the answer.
export async function loadSpoolmanCatalog(): Promise<SpoolmanCatalog> {
  await post('/api/spoolman/catalog/refresh')
  let catalog = (await request('/api/spoolman/catalog')) as SpoolmanCatalog
  for (let i = 0; catalog.loading && i < 40; i++) {
    await new Promise(done => setTimeout(done, 500))
    catalog = (await request('/api/spoolman/catalog')) as SpoolmanCatalog
  }
  return catalog
}
// What the printer's last sync with Spoolman did (GET /api/spoolman). It only reads the printer's memory, not Spoolman.
// One shared copy: the status line of the Spoolman card and the list of unsent usage read the same one, so a page that
// is refreshed often still asks the printer once per poll.
export const syncSpoolman = () => post('/api/spoolman/sync')
export type SpoolmanStatus = {
  busy?: boolean // running worker or results waiting to be applied; absent on older backends
  enabled: boolean
  configured: boolean
  last_sync: number // seconds; 0 = no sync yet under the current settings
  result: string // never | ok | unreachable | unauthorized | forbidden | not_found | server_error | refused | bad_reply | too_large | timeout | bad_url
  http: number
  list?: { no_weight?: number; library_full?: number }
}
export const spoolmanState = store({ data: null as SpoolmanStatus | null })
export async function refreshSpoolmanStatus() {
  try {
    spoolmanState.set({ data: (await request('/api/spoolman')) as SpoolmanStatus })
  } catch {
    /* the last answer stays; the next poll asks again */
  }
}

type StatusLine = { ok: boolean; text: Key; hint?: Key; vars: { n?: number; http?: number } }
const PROBLEMS: Record<string, { text: Key; hint?: Key }> = {
  bad_url: { text: 'spools.sm_bad_url', hint: 'spools.sm_bad_url_hint' },
  unauthorized: { text: 'spools.sm_login', hint: 'spools.sm_login_hint' },
  forbidden: { text: 'spools.sm_login', hint: 'spools.sm_login_hint' },
  unreachable: { text: 'spools.sm_unreachable', hint: 'spools.sm_unreachable_hint' },
  timeout: { text: 'spools.sm_timeout', hint: 'spools.sm_timeout_hint' },
  bad_reply: { text: 'spools.sm_bad_reply', hint: 'spools.sm_bad_reply_hint' },
  too_large: { text: 'spools.sm_too_large', hint: 'spools.sm_too_large_hint' },
  not_found: { text: 'spools.sm_not_found', hint: 'spools.sm_not_found_hint' },
  server_error: { text: 'spools.sm_server' },
  refused: { text: 'spools.sm_refused' },
}
// One line for the Spoolman card, or none (sync off, no address, nothing read yet). `result` is the first thing that went
// wrong in the last cycle; only when it is "ok" do the counters of the list say anything. Rank: result, then spools
// without a weight, then a full library.
export function spoolmanLine(s: SpoolmanStatus | null): StatusLine | null {
  if (!s?.enabled || !s.configured) return null
  if (s.busy) return { ok: true, text: 'spools.sm_syncing', vars: {} }
  const problem = PROBLEMS[s.result]
  if (problem) return { ok: false, ...problem, vars: { http: s.http } }
  if (s.result === 'never') return { ok: true, text: 'spools.sm_never', vars: {} }
  if (s.result !== 'ok') return null
  const noWeight = s.list?.no_weight ?? 0
  const full = s.list?.library_full ?? 0
  if (noWeight > 0)
    return { ok: false, text: 'spools.sm_no_weight', hint: 'spools.sm_no_weight_hint', vars: { n: noWeight } }
  if (full > 0)
    return { ok: false, text: 'spools.sm_library_full', hint: 'spools.sm_library_full_hint', vars: { n: full } }
  return { ok: true, text: 'spools.sm_ok', vars: {} }
}

// The printer's refusals of the Spoolman functions carry a stable `code` (docs/API.md); the English `error` stays for
// everyone else. A code this page does not know falls back to that English text.
const REFUSALS: Record<string, Key> = {
  spoolman_bad_url: 'spools.err_bad_url',
  spoolman_auth_too_long: 'spools.err_auth_too_long',
  spoolman_bad_flag: 'spools.err_bad_flag',
  spoolman_held_full: 'spools.err_held_full',
  spoolman_off: 'spools.err_off',
  spoolman_catalog_off: 'spools.err_off',
  spoolman_held_unknown: 'spools.err_held_unknown',
  spoolman_held_twice: 'spools.err_held_twice',
  spoolman_pick_spool: 'spools.err_pick_spool',
  spoolman_held_unsure: 'spools.err_held_unsure',
  spoolman_held_sending: 'spools.err_held_sending',
  spoolman_busy: 'spools.err_busy',
  spoolman_start_failed: 'spools.err_start_failed',
  spoolman_no_old: 'spools.err_no_old',
  spoolman_no_url: 'spools.err_no_url',
  spoolman_catalog_too_large: 'spools.err_catalog_too_large',
  spoolman_settings_changed: 'spools.held_settings_changed',
}
const refusalKey = (error: any): Key | undefined => {
  const code = String(error?.data?.code)
  return Object.keys(REFUSALS).includes(code) ? REFUSALS[code] : undefined
}
// The text of a refusal in the page's language; without a known code, the printer's English text as before.
export const refusalText = (error: any) => {
  const key = refusalKey(error)
  return key ? t(key) : errText(error)
}
