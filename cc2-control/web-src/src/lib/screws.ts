export const SCREW_SETTLE_S = 30
// Minutes without a session action after which CC2 Control ends the session and switches both heaters off
// (SCREWS_SESSION_TIMEOUT_S 600 in cc2-control/src/control.h).
export const SCREW_SESSION_TIMEOUT_MIN = 10

// The hold time before a screw measurement: the printer service takes "screws:soak:<minutes>:" in front of
// screws:measure…, screws:measure:corner:… and screws:tram:start…, heats the bed, holds it that long and then runs the
// action (GET /api/printer reports it as screws_soak). 0 minutes sends the action alone.
const SOAK_PREFIX = /^screws:soak:\d+:/
export const withSoak = (action: string, minutes: number) =>
  minutes > 0 ? `screws:soak:${Math.round(minutes)}:${action}` : action
export const stripSoak = (action: string) => action.replace(SOAK_PREFIX, '')

// The measuring points are the centres of the four bed screws as measured on a CC2 (SCREW_CORNERS in
// cc2-control/src/control.c). The earlier points 30/30, 230/30, 230/225, 30/225 lay 2 to 5 mm beside them.
export const SCREW_KEYS = ['27.9,25.95', '227.15,25.95', '227.15,225.5', '27.9,225.5'] as const
export const SCREW_CORNERS = [
  { id: 'FL', key: '27.9,25.95', x: 27.9, y: 25.95, n: '1' },
  { id: 'FR', key: '227.15,25.95', x: 227.15, y: 25.95, n: '2' },
  { id: 'RR', key: '227.15,225.5', x: 227.15, y: 225.5, n: '3' },
  { id: 'RL', key: '27.9,225.5', x: 27.9, y: 225.5, n: '4' },
] as const
// A reported probe position belongs to a corner when it lies this close to it in X and in Y (mm), as in control.c.
export const SCREW_CORNER_MATCH_MM = 1.5
export type ScrewId = (typeof SCREW_CORNERS)[number]['id']

const isScrewKey = (k: string): k is (typeof SCREW_KEYS)[number] => (SCREW_KEYS as readonly string[]).includes(k)

// The key of the corner a probe position belongs to; '' for a position that is no screw corner (the earlier measuring
// points included: old and new points are never mixed).
const keyOf = (x: number, y: number) =>
  SCREW_CORNERS.find(c => Math.abs(x - c.x) <= SCREW_CORNER_MATCH_MM && Math.abs(y - c.y) <= SCREW_CORNER_MATCH_MM)
    ?.key ?? ''

// One number as the printer writes it ("-0.700000"). A token that is not exactly a number ("1.5e-02" is fine, "-0,7" and
// "+" signs are not guessed) makes the whole line count as unreadable instead of being cut at the first odd character.
const NUMBER = /^-?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?$/i
const readNumber = (token: string): number | null => (NUMBER.test(token) ? Number(token) : null)
// The console reply wraps every line in JSON (`{"id":0,"report":{…"message":"// Result is z=-0.569811"}}`): the value ends at the
// first space, quote, brace or bracket. A comma stays part of it, so "-0,7" is still refused.
const VALUE = String.raw`[^\s"'}\]]+`

// The printer writes heights with six decimals: averages are cut back to that, so equal readings average to themselves.
const round6 = (v: number) => Math.round(v * 1e6) / 1e6

// Average of the last three probes at each of the four screw positions, parsed from console output (the printer's own Result is
// the average of its probes: seen in 127 of 127 results of one measurement run).
export function screwSamples(text: string): Record<string, number> {
  const re = new RegExp(String.raw`probe at\s+(\S+?),\s*(\S+)\s+is z=(${VALUE})`, 'gi'),
    groups = new Map<string, number[]>()
  for (const m of (text || '').matchAll(re)) {
    const x = readNumber(m[1]),
      y = readNumber(m[2]),
      z = readNumber(m[3])
    if (x == null || y == null || z == null) continue
    const k = keyOf(x, y)
    if (!isScrewKey(k)) continue
    if (!groups.has(k)) groups.set(k, [])
    groups.get(k)?.push(z)
  }
  const out: Record<string, number> = {}
  for (const [k, v] of groups) if (v.length >= 3) out[k] = round6(v.slice(-3).reduce((a, b) => a + b, 0) / 3)
  return out
}

export function screwValues(text: string): Record<string, number> | null {
  const out = screwSamples(text)
  return Object.keys(out).length === 4 ? out : null
}

// samples: the single probes of this PROBE, kept only when their average is its Result (within 1 µm).
export type ProbeResult = { key: string; z: number; samples: number[] }
const PROBE_TOLERANCE_MM = 0.001

// One entry per PROBE command, using the last in-range "probe at" coordinates before "Result is z=". The remembered corner
// is cleared after every result and by every "probe at" line outside the four screws, so a result never lands on a
// corner it does not belong to.
export function probeResults(text: string): ProbeResult[] {
  const out: ProbeResult[] = []
  let lastKey = ''
  let samples: number[] = []
  for (const line of (text || '').split(/\r?\n/)) {
    const at = new RegExp(String.raw`probe at\s+(\S+?),\s*(\S+)\s+is z=(${VALUE})`, 'i').exec(line)
    if (at) {
      const x = readNumber(at[1]),
        y = readNumber(at[2])
      const k = x != null && y != null ? keyOf(x, y) : ''
      lastKey = isScrewKey(k) ? k : ''
      const v = readNumber(at[3])
      if (lastKey && v != null) samples.push(v)
      else samples = []
    }
    const result = new RegExp(`Result is z=(${VALUE})`, 'i').exec(line)
    if (result) {
      const z = readNumber(result[1])
      const mean = samples.length ? samples.reduce((a, b) => a + b, 0) / samples.length : NaN
      if (lastKey && z != null)
        out.push({ key: lastKey, z, samples: Math.abs(mean - z) <= PROBE_TOLERANCE_MM ? samples : [] })
      lastKey = ''
      samples = []
    }
  }
  return out
}

// One height per corner. With "2× 3 probes" a corner has two results (the firmware averages the three probes of each
// PROBE): the corner's value is the average of both, that is of all six probes.
export function cornerHeights(text: string): Record<string, number> {
  const results = probeResults(text)
  if (results.length) {
    const sums: Record<string, { sum: number; n: number }> = {}
    for (const r of results) {
      const at = sums[r.key] ?? { sum: 0, n: 0 }
      at.sum += r.z
      at.n++
      sums[r.key] = at
    }
    const out: Record<string, number> = {}
    for (const [key, { sum, n }] of Object.entries(sums)) out[key] = round6(sum / n)
    return out
  }
  return screwSamples(text)
}

// How the corners of this console output were measured: twice (two results) or once, and the single probes behind them
// (all of them, or null when one PROBE's probes do not add up to its Result or are missing).
export function cornerReads(text: string): Record<string, { twice: boolean; probes: number[] | null }> {
  const out: Record<string, { twice: boolean; probes: number[] | null }> = {}
  const bag: Record<string, { n: number; probes: number[]; ok: boolean }> = {}
  for (const r of probeResults(text)) {
    const at = bag[r.key] ?? { n: 0, probes: [], ok: true }
    at.n++
    if (r.samples.length) at.probes.push(...r.samples)
    else at.ok = false
    bag[r.key] = at
  }
  for (const [key, at] of Object.entries(bag))
    out[key] = { twice: at.n >= 2, probes: at.ok && at.probes.length ? at.probes : null }
  return out
}

// What the table says about how sure a corner's difference is: the fixed "±" per kind of measurement (µm; 1×3 and 2×3 = the
// average of six, measured as the spread of a corner's difference to front left), the limit of the turn instruction
// that it is shown around, and the spread of a corner's probes (max minus min) above which the corner is called deviating.
export const TURN_LIMIT_UM = 20
export const TOLERANCE_UM = { once: 5, twice: 3 }
export const PROBE_SPREAD_LIMIT_UM = 10
export const probeSpreadUm = (probes: number[]) => Math.round((Math.max(...probes) - Math.min(...probes)) * 1000)

// Heights come with six decimals (whole nanometres). Subtracting two of them leaves noise of about 1e-16 mm, which
// decided the threshold comparisons below on an exact tie. Everything is rounded back to whole nanometres first.
const nm = (mm: number) => Math.round(mm * 1e6)
const fromNm = (n: number) => n / 1e6
// Whole micrometres, halves away from zero (Math.round would send -0.5 to 0 and -1.5 to -1).
const wholeUm = (mm: number) => Math.floor(Math.abs(nm(mm)) / 1000 + 0.5)

export function screwPlan(values: Record<string, number>) {
  const ref = values[SCREW_KEYS[0]],
    standard = SCREW_KEYS.map(k => fromNm(nm(values[k] - ref))),
    other = standard.slice(1)
  const sameSign = other.every(v => nm(v) > 20000) || other.every(v => nm(v) < -20000)
  const common = sameSign ? Math.sign(other[0]) * Math.min(...other.map(Math.abs)) : 0
  // The other corners are shown relative to a front-left corner that has been moved towards them by
  // `common`; front-left itself therefore moves by -common (same legend: positive = lower this corner).
  const optimized = [-common, ...other.map(v => fromNm(nm(v - common)))]
  const peak = (l: number[]) => Math.max(...l.map(Math.abs))
  const useOptimized = sameSign && nm(Math.abs(common)) <= 500000 && nm(peak(standard)) - nm(peak(optimized)) > 5000
  return {
    shown: useOptimized ? optimized : standard,
    useOptimized,
    common,
    before: wholeUm(peak(standard)),
    best: wholeUm(peak(optimized)),
  }
}
// The unit the page shows heights in. Micrometres in whole numbers is what the page always showed; millimetres with
// three decimals is the way the mesh pages and the raw probing values write the same heights.
export type HeightUnit = 'um' | 'mm'
export const parseHeightUnit = (v: unknown): HeightUnit => (v === 'mm' ? 'mm' : 'um')
// A height in millimetres as text with its unit right behind the number (a no-break space keeps them together).
// One micrometre is the resolution of both writings, so nothing is rounded that was not rounded before.
// The decimal sign follows the interface language.
const mmFormats = new Map<string, Intl.NumberFormat>()
const decimals = (value: number, digits: number, lang: string) => {
  const key = `${lang}/${digits}`
  let f = mmFormats.get(key)
  if (!f) {
    try {
      f = new Intl.NumberFormat(lang, {
        minimumFractionDigits: digits,
        maximumFractionDigits: digits,
        useGrouping: false,
      })
    } catch {
      f = new Intl.NumberFormat('en', {
        minimumFractionDigits: digits,
        maximumFractionDigits: digits,
        useGrouping: false,
      })
    }
    mmFormats.set(key, f)
  }
  return f.format(value)
}
// The number of heightLabel without its unit (a table states the unit once in its header).
export const heightNumber = (mm: number, unit: HeightUnit, withSign = false, lang = 'en') => {
  const um = wholeUm(mm)
  const sign = um === 0 ? '' : mm < 0 ? '−' : withSign ? '+' : ''
  return unit === 'mm' ? `${sign}${decimals(um / 1000, 3, lang)}` : `${sign}${um}`
}
export const heightLabel = (mm: number, unit: HeightUnit, withSign = false, lang = 'en') =>
  `${heightNumber(mm, unit, withSign, lang)}\u00a0${unit === 'mm' ? 'mm' : 'µm'}`

// --- Turning instruction (accepted display A2) ---------------------------------------------------------------
// m is the movement a corner needs in micrometres (positive = raise), P the height per turn in micrometres. One click
// of the tool is 5 degrees (72 clicks per turn). The exact angle is never rounded; the reading aid (whole clicks) names its rest.
export const CLICK_DEG = 5
export const QUARTER_CLICKS = 18
export const TURN_TOLERANCE_UM = 20
export const ONE_GO_DEG = 180
export const COARSE_SAFETY = 0.97
export type TurnFine = { n: number; q: number; r: number; deg: number; um: number }
export type TurnStage = {
  stage: 0 | 1 | 2 | 3
  dir: 'raise' | 'lower'
  theta: number // exact angle in degrees
  fine?: TurnFine // stages 1, 2: the reading aid; stage 3: the preview of the fine step after the coarse one
  rest: number // µm still missing by the reading aid (signed like m); stage 3: after the fine preview
  coarse?: number // stage 3: the coarse step in degrees
  afterCoarse?: number // stage 3: µm left after the coarse step (signed like m)
}
const fineOf = (thetaDeg: number, umPerDeg: number): TurnFine => {
  const n = Math.floor(thetaDeg / CLICK_DEG + 0.5)
  const q = Math.floor(n / QUARTER_CLICKS)
  return { n, q, r: n - q * QUARTER_CLICKS, deg: n * CLICK_DEG, um: n * CLICK_DEG * umPerDeg }
}
export function turnPlan(m: number, pitchUm: number): TurnStage {
  const um = Math.abs(m)
  const umPerDeg = pitchUm / 360
  const theta = um / umPerDeg
  const dir = m > 0 ? 'raise' : 'lower'
  // The two thresholds are decided on whole picometres ("times 360 before dividing"): exactly 20 um and exactly
  // 180 degrees must not depend on 1e-13 noise (heights have 6 decimals = 1 nm). The shown values stay exact.
  const pmAbs = Math.round(um * 1e6)
  if (!(pmAbs > TURN_TOLERANCE_UM * 1e6)) return { stage: 0, dir, theta, rest: m }
  const sign = Math.sign(m)
  if (pmAbs * 360 <= ONE_GO_DEG * pitchUm * 1e6) {
    const fine = fineOf(theta, umPerDeg)
    return { stage: fine.deg < 90 ? 1 : 2, dir, theta, fine, rest: sign * (um - fine.um) }
  }
  const coarse = Math.floor((COARSE_SAFETY * theta) / 90) * 90
  const left = um - coarse * umPerDeg
  const fine = fineOf(left / umPerDeg, umPerDeg)
  return { stage: 3, dir, theta, fine, coarse, afterCoarse: sign * left, rest: sign * (left - fine.um) }
}

// Auto: what can really be turned (whole clicks, quarters, the rest in µm; a coarse step first over half a turn).
// Exact: the exact value in the chosen unit, never rounded to clicks, in one go.
export type TurnMode = 'auto' | 'exact'
export type TurnUnit = 'degrees' | 'clicks' | 'fractions' | 'clock' | 'clockhours'
export const TURN_MODES: readonly TurnMode[] = ['auto', 'exact']
export const TURN_UNITS: readonly TurnUnit[] = ['degrees', 'clicks', 'fractions', 'clock', 'clockhours']
export const parseTurnMode = (v: unknown): TurnMode => (v === 'exact' ? 'exact' : 'auto')
export const parseTurnUnit = (v: unknown): TurnUnit =>
  v === 'clicks' || v === 'fractions' || v === 'clock' || v === 'clockhours' ? v : 'degrees'

// The clock read like an hour hand: the handle starts at 12 and ends at a time on the clock face, one turn is twelve
// hours. Rounded to quarter hours (7.5 degrees, about 15 µm at 0.7 mm pitch); whole turns are counted separately.
// `ccw`: the turn goes against the clock (the direction is decided by the caller), so the hand runs backwards from 12:
// 1.5 hours against the clock end at 10:30.
export const clockHours = (thetaDeg: number, ccw = false) => {
  const quarters = Math.max(0, Math.round(thetaDeg / 7.5))
  const turns = Math.floor(quarters / 48)
  const rest = quarters % 48
  const face = ccw && rest ? 48 - rest : rest
  return {
    turns,
    onTwelve: rest === 0,
    time: `${Math.floor(face / 4) || 12}:${String((face % 4) * 15).padStart(2, '0')}`,
  }
}
// The exact value that leads to zero, in the chosen unit. Every unit comes from the same exact angle: one decimal
// for degrees, clicks and clock minutes (0.1 degree is about 0.2 µm, far below the measuring scatter).
export const turnExact = (thetaDeg: number, unit: TurnUnit, lang = 'en', ccw = false) => {
  const one = (v: number) => decimals(v, 1, lang)
  if (unit === 'degrees') return `${one(thetaDeg)}°`
  if (unit === 'clicks') return one(thetaDeg / CLICK_DEG)
  if (unit === 'fractions') {
    const turns = thetaDeg / 360
    return `${decimals(turns, 3, lang)}`
  }
  if (unit === 'clockhours') {
    const c = clockHours(thetaDeg, ccw)
    return c.turns ? `${c.turns} + ${c.time}` : c.time
  }
  // Like Klipper: hours without a leading zero, whole minutes (rounded, carried over properly), minutes with two digits.
  const minutes = Math.round(thetaDeg / 6) // 60 minutes = one turn
  return `${Math.floor(minutes / 60)}:${String(minutes % 60).padStart(2, '0')}`
}
// The fraction nearest to a turn count on the clock grid (twelfths), written like Auto does: "1 + 1/12", "1/4", "0".
export const turnFraction = (thetaDeg: number): string => turnFractionAid(thetaDeg).text
// Auto in fractions: only fractions of a clock face (multiples of 30 degrees): whole turns and quarters (1/4, 1/2, 3/4)
// plus at most one small fraction (1/12 or 1/6). The combination with the smallest
// rest wins; on a tie the one with fewer parts, then the smaller angle.
const QUARTER_NAMES = ['', '1/4', '1/2', '3/4']
const SMALL_FRACTIONS: readonly { deg: number; text: string }[] = [
  { deg: 0, text: '' },
  { deg: 30, text: '1/12' },
  { deg: 60, text: '1/6' },
]
export type TurnFractionAid = { deg: number; text: string }
// The turn count of an angle as a decimal number (at most two decimals, the decimal sign follows the language).
export const turnsNumber = (deg: number, lang = 'en') => {
  try {
    return new Intl.NumberFormat(lang, { maximumFractionDigits: 2, useGrouping: false }).format(deg / 360)
  } catch {
    return new Intl.NumberFormat('en', { maximumFractionDigits: 2, useGrouping: false }).format(deg / 360)
  }
}
export const turnFractionAid = (thetaDeg: number): TurnFractionAid => {
  let best: (TurnFractionAid & { err: number; parts: number }) | null = null
  const maxQuarters = Math.max(0, Math.ceil(thetaDeg / 90) + 1)
  for (let k = 0; k <= maxQuarters; k++) {
    for (const small of SMALL_FRACTIONS) {
      const deg = k * 90 + small.deg
      const err = Math.abs(thetaDeg - deg)
      const whole = Math.floor(k / 4)
      const parts = (whole ? 1 : 0) + (k % 4 ? 1 : 0) + (small.deg ? 1 : 0)
      if (
        best &&
        (err > best.err + 1e-9 ||
          (Math.abs(err - best.err) <= 1e-9 && (parts > best.parts || (parts === best.parts && deg >= best.deg))))
      )
        continue
      const text = [whole || '', QUARTER_NAMES[k % 4], small.text].filter(x => x !== '').join(' + ') || '0'
      best = { deg, text, err, parts }
    }
  }
  return { deg: best?.deg ?? 0, text: best?.text ?? '0' }
}
export const microns = (v: number) => {
  const um = wholeUm(v)
  return `${um === 0 ? '' : v > 0 ? '+' : '−'}${um} µm`
}

// The height every corner is adjusted to: the front-left height, moved by the common shift when the combined plan
// (front left moves as well) is used. Millimetres, in the measured values' own frame.
export const screwTarget = (values: Record<string, number>, plan: ReturnType<typeof screwPlan>) =>
  values[SCREW_KEYS[0]] + (plan.useOptimized ? plan.common : 0)

export type ScrewIst = Partial<Record<'bed' | 'nozzle' | 'chamber', number>>

export type ScrewHistoryMeasurement = {
  time: number
  kind: 'start' | 'probe' | 'check' | 'quick'
  corner: ScrewId | null
  twice: boolean
  z: Partial<Record<ScrewId, number>>
  z2?: Partial<Record<ScrewId, number>>
  // The single probes of a corner in this measurement (three, six with 2× 3), when the printer service wrote them down.
  probes?: Partial<Record<ScrewId, number[]>>
  bed?: number
  // The actual temperatures at the last result of the job; only the sensors that had a value.
  ist?: ScrewIst
}

export type ScrewHistoryEntryKind = 'session' | 'quick'

export type ScrewHistorySession = {
  // Missing on an older backend. Callers treat that as a session.
  kind: ScrewHistoryEntryKind
  started: number
  ended: number | null
  measurements: ScrewHistoryMeasurement[]
  // Where the corners were probed: at the screw centres, or at the earlier points 30/30, 230/30, 230/225, 30/225
  // (entries written before the points moved, and every entry of a backend that does not say).
  points: 'screws' | 'old'
}

const SCREW_IDS: ScrewId[] = ['FL', 'FR', 'RR', 'RL']
const isScrewId = (v: unknown): v is ScrewId => SCREW_IDS.includes(v as ScrewId)

const zBag = (v: unknown): Partial<Record<ScrewId, number>> => {
  const out: Partial<Record<ScrewId, number>> = {}
  if (!v || typeof v !== 'object') return out
  for (const id of SCREW_IDS) {
    const n = (v as Record<string, unknown>)[id]
    if (typeof n === 'number' && Number.isFinite(n)) out[id] = n
  }
  return out
}

const probeBag = (v: unknown): Partial<Record<ScrewId, number[]>> | null => {
  if (!v || typeof v !== 'object') return null
  const out: Partial<Record<ScrewId, number[]>> = {}
  for (const id of SCREW_IDS) {
    const list = (v as Record<string, unknown>)[id]
    if (
      Array.isArray(list) &&
      list.length > 0 &&
      list.length <= 6 &&
      list.every(n => typeof n === 'number' && Number.isFinite(n))
    )
      out[id] = list as number[]
  }
  return Object.keys(out).length ? out : null
}

const IST_KEYS = ['bed', 'nozzle', 'chamber'] as const
// Missing on entries from before the actual temperatures were recorded; implausible numbers are left out.
const istBag = (v: unknown): ScrewIst | null => {
  if (!v || typeof v !== 'object') return null
  const out: ScrewIst = {}
  for (const key of IST_KEYS) {
    const n = (v as Record<string, unknown>)[key]
    if (typeof n === 'number' && Number.isFinite(n) && n >= -50 && n <= 400) out[key] = n
  }
  return Object.keys(out).length ? out : null
}

const measurementKind = (v: unknown): ScrewHistoryMeasurement['kind'] | null =>
  v === 'start' || v === 'probe' || v === 'check' || v === 'quick' ? v : null

// Sessions and quick measurements have separate caps. An entry with no kind is an older session.
export function parseScrewHistory(data: unknown): ScrewHistorySession[] {
  const list = (data as { sessions?: unknown } | null)?.sessions
  if (!Array.isArray(list)) return []
  const sessions: ScrewHistorySession[] = []
  let sessionCount = 0
  let quickCount = 0
  for (const raw of list) {
    if (!raw || typeof raw !== 'object') continue
    const s = raw as Record<string, unknown>
    const kind: ScrewHistoryEntryKind | null =
      s.kind === 'quick' ? 'quick' : s.kind === 'session' || s.kind == null ? 'session' : null
    if (!kind) continue
    if (kind === 'quick' ? quickCount >= 10 : sessionCount >= 20) continue
    if (kind === 'quick') quickCount++
    else sessionCount++
    const measurements: ScrewHistoryMeasurement[] = []
    if (Array.isArray(s.measurements)) {
      for (const item of s.measurements) {
        if (!item || typeof item !== 'object') continue
        const m = item as Record<string, unknown>
        const measured = measurementKind(m.kind)
        if (!measured || typeof m.time !== 'number') continue
        const twice = m.twice === true
        measurements.push({
          time: m.time,
          kind: measured,
          corner: isScrewId(m.corner) ? m.corner : null,
          twice,
          z: zBag(m.z),
          ...(twice ? { z2: zBag(m.z2) } : {}),
          ...(probeBag(m.probes) ? { probes: probeBag(m.probes) as Partial<Record<ScrewId, number[]>> } : {}),
          // The bed target the job measured at; missing on entries from before it was recorded.
          ...(typeof m.bed === 'number' && Number.isInteger(m.bed) && m.bed >= 0 && m.bed <= 120 ? { bed: m.bed } : {}),
          ...(istBag(m.ist) ? { ist: istBag(m.ist) as ScrewIst } : {}),
        })
      }
    }
    sessions.push({
      kind,
      started: typeof s.started === 'number' ? s.started : 0,
      ended: typeof s.ended === 'number' ? s.ended : null,
      measurements,
      points: s.points === 'screws' ? 'screws' : 'old',
    })
  }
  return sessions
}

// The reading of a measurement: its heights, and with "2× 3 probes" the average of both results of every corner.
export function readingBag(m: ScrewHistoryMeasurement): Partial<Record<ScrewId, number>> {
  if (!m.twice || !m.z2) return m.z
  const out: Partial<Record<ScrewId, number>> = { ...m.z }
  for (const id of SCREW_IDS) {
    const a = m.z[id],
      b = m.z2[id]
    if (typeof a === 'number' && typeof b === 'number') out[id] = (a + b) / 2
    else if (typeof b === 'number') out[id] = b
  }
  return out
}

// Offsets against the front-left raw Z, in millimetres, oldest first.
// A single-corner probe keeps the last front-left value. Repeated identical micrometres are dropped.
export function cornerOffsets(session: ScrewHistorySession): Record<ScrewId, number[]> {
  const raw: Partial<Record<ScrewId, number>> = {}
  const series: Record<ScrewId, number[]> = { FL: [], FR: [], RR: [], RL: [] }
  for (const m of session.measurements) {
    if (m.kind === 'quick') continue
    const bags = [readingBag(m)]
    for (const bag of bags) {
      for (const id of SCREW_IDS) if (typeof bag[id] === 'number') raw[id] = bag[id]
      if (typeof raw.FL !== 'number') continue
      const fl = raw.FL
      const touched: ScrewId[] = m.kind === 'probe' && m.corner ? [m.corner] : [...SCREW_IDS]
      for (const id of touched) {
        if (typeof bag[id] !== 'number') continue
        const delta = id === 'FL' ? 0 : bag[id] - fl
        const list = series[id]
        const um = Math.round(delta * 1000)
        const prev = list.length ? Math.round(list[list.length - 1] * 1000) : null
        if (prev !== um) list.push(delta)
      }
    }
  }
  return series
}

// The offsets of every measurement of a session against the front-left height at that moment, one entry per measurement
// (same running reading as cornerOffsets). A corner the measurement did not touch is null.
export function sessionRowOffsets(session: ScrewHistorySession): Record<ScrewId, number | null>[] {
  const raw: Partial<Record<ScrewId, number>> = {}
  return session.measurements.map(m => {
    const out: Record<ScrewId, number | null> = { FL: null, FR: null, RR: null, RL: null }
    if (m.kind === 'quick') return measurementOffsets(m)
    const bag = readingBag(m)
    for (const id of SCREW_IDS) if (typeof bag[id] === 'number') raw[id] = bag[id]
    if (typeof raw.FL !== 'number') return out
    const touched: ScrewId[] = m.kind === 'probe' && m.corner ? [m.corner] : [...SCREW_IDS]
    for (const id of touched) if (typeof bag[id] === 'number') out[id] = id === 'FL' ? 0 : (bag[id] as number) - raw.FL
    return out
  })
}

const signedUm = (v: number) => `${v > 0 ? '+' : v < 0 ? '−' : ''}${Math.abs(Math.round(v * 1000))}`
const umLabel = (um: number) => `${um > 0 ? '+' : um < 0 ? '−' : ''}${Math.abs(um)}`

export type ReferenceMove = {
  // "0 → +2 µm": the latest front-left z against the first z of this session (with 2× 3 probes the average of both).
  change: string
  // The same move in millimetres (latest minus first front-left height), for a display in the chosen unit.
  changeMm: number
  // How far front left has moved since the last full measurement. Null once a later check caught up.
  sinceFullUm: number | null
}

// The measurement reference is the raw front-left height. A single re-probe of that corner is zero
// against itself, so the row shows how far that raw height has moved during the session.
export function referenceMove(session: ScrewHistorySession): ReferenceMove | null {
  let first: number | undefined
  let current: number | undefined
  let full: number | undefined
  let pending = false
  let readings = 0
  for (const m of session.measurements) {
    if (m.kind === 'quick') continue
    const fl = readingBag(m).FL
    if (typeof fl !== 'number') continue
    readings++
    if (first === undefined) first = fl
    current = fl
    if (m.kind === 'start' || m.kind === 'check') {
      full = fl
      pending = false
    } else if (m.kind === 'probe' && m.corner === 'FL') pending = true
  }
  // A single front-left reading (the session start) has no shift to show yet.
  if (first === undefined || current === undefined || readings < 2) return null
  const changeUm = Math.round((current - first) * 1000)
  const since = pending && full !== undefined ? Math.round((current - full) * 1000) : null
  return { change: `0 → ${umLabel(changeUm)} µm`, changeMm: changeUm / 1000, sinceFullUm: since }
}

// A corner counts as "hardly moved" between two measurements when the page had given a turn of at least STUCK_MIN_UM
// for it and its offset to front left changed by less than STUCK_RATIO of that turn (pages/bed then says at the corner
// that the sleeve may turn with the screw).
export const STUCK_MIN_UM = 100
export const STUCK_RATIO = 0.25
// The corners (index 1 to 3; front left is the reference) that hardly moved: `before` and `now` are the offsets to front
// left of two measurements (µm), `turns` the turn the page gave for each corner after `before` (µm, the sign is ignored).
export const stuckCorners = (before: number[], now: number[], turns: number[]) =>
  [1, 2, 3].filter(
    i =>
      Number.isFinite(turns[i]) &&
      Math.abs(turns[i]) >= STUCK_MIN_UM &&
      Math.abs(now[i] - before[i]) < STUCK_RATIO * Math.abs(turns[i])
  )

// A corner counts as turned once its offset to front left changed by this much since the reference, however far off it
// was before. Measured on the printer (17.11.2026): without turning, the offset of a corner changes by about ±6 µm
// between two measurements, and it wanders by up to about 20 µm in 20 minutes while the bed soaks; 25 µm lies above both.
const TURNED_MIN_UM = 25

// Whether the screws were turned after `since` (Unix seconds, the working mesh), judged from screw measurements alone,
// never against a mesh: the first full measurement after it is the reference, and a later measurement in which a
// corner moved against front left (as cornerOffsets counts it) by at least TURNED_MIN_UM counts as turned. A single
// first measurement is no turn. "Hardly moved" (STUCK_RATIO, pages/bed) is a question of its own.
export function screwsTurnedSince(sessions: ScrewHistorySession[], since: number): boolean {
  const list = sessions
    .flatMap(s => s.measurements)
    .filter(m => m.time > since)
    .sort((a, b) => a.time - b.time)
  const raw: Partial<Record<ScrewId, number>> = {}
  let reference: Partial<Record<ScrewId, number>> | null = null
  for (const m of list) {
    const bag = readingBag(m)
    for (const id of SCREW_IDS) if (typeof bag[id] === 'number') raw[id] = bag[id]
    const fl = raw.FL
    if (typeof fl !== 'number') continue
    const single = m.corner !== null && (m.kind === 'probe' || m.kind === 'quick')
    const full = SCREW_IDS.every(id => typeof bag[id] === 'number')
    const touched = (single && m.corner ? [m.corner] : SCREW_IDS).filter(
      id => id !== 'FL' && typeof bag[id] === 'number'
    )
    const offsets: Partial<Record<ScrewId, number>> = {}
    for (const id of touched) offsets[id] = ((raw[id] as number) - fl) * 1000
    if (!reference) {
      if (full && !single) reference = offsets
      continue
    }
    for (const id of touched) {
      const before = reference[id]
      const now = offsets[id]
      if (typeof before !== 'number' || typeof now !== 'number') continue
      if (Math.abs(now - before) >= TURNED_MIN_UM) return true
    }
  }
  return false
}

// Last five offsets, one unit at the end: "−188 → −62 → −8 µm".
export function offsetChain(values: number[], limit = 5): string {
  const last = values.slice(-limit)
  if (!last.length) return ''
  return `${last.map(signedUm).join(' → ')} µm`
}

const csvPad = (n: number) => String(n).padStart(2, '0')

// "2026-11-10 20:40:54" in the browser's local time, beside the Unix timestamps.
export function screwTimeLocal(unix: number): string {
  const d = new Date(unix * 1000)
  const date = `${d.getFullYear()}-${csvPad(d.getMonth() + 1)}-${csvPad(d.getDate())}`
  const clock = `${csvPad(d.getHours())}:${csvPad(d.getMinutes())}:${csvPad(d.getSeconds())}`
  return `${date} ${clock}`
}

// One quick measurement against its own front-left sample. Not part of a session trail.
export function measurementOffsets(m: ScrewHistoryMeasurement): Record<ScrewId, number | null> {
  const bag = readingBag(m)
  const fl = bag.FL
  const out: Record<ScrewId, number | null> = { FL: null, FR: null, RR: null, RL: null }
  if (typeof fl !== 'number') return out
  for (const id of SCREW_IDS) {
    const z = bag[id]
    if (typeof z !== 'number') continue
    out[id] = id === 'FL' ? 0 : z - fl
  }
  return out
}

const CSV_HEADER = [
  'started',
  'ended',
  'entry_kind',
  'time',
  'time_local',
  'kind',
  'corner',
  'twice',
  'pass',
  'FL_measured_mm',
  'FR_measured_mm',
  'RR_measured_mm',
  'RL_measured_mm',
  'FL_table_um',
  'FR_table_um',
  'RR_table_um',
  'RL_table_um',
]

// excel uses ';' and a decimal comma so Excel (DE) opens the columns without a conversion step.
export function screwHistoryCsv(sessions: ScrewHistorySession[], style: 'comma' | 'excel' = 'comma'): string {
  const sep = style === 'excel' ? ';' : ','
  const num = (n: number) => (style === 'excel' ? String(n).replace('.', ',') : String(n))
  const lines = [CSV_HEADER.join(sep)]
  for (const s of sessions) {
    const raw: Partial<Record<ScrewId, number>> = {}
    for (const m of s.measurements) {
      const bags = [m.z, ...(m.twice && m.z2 ? [m.z2] : [])]
      bags.forEach((bag, index) => {
        for (const id of SCREW_IDS) if (typeof bag[id] === 'number') raw[id] = bag[id]
        const fl = raw.FL
        const um = (id: ScrewId) => {
          if (typeof raw[id] !== 'number' || typeof fl !== 'number') return ''
          return String(Math.round((id === 'FL' ? 0 : raw[id] - fl) * 1000))
        }
        const mm = (id: ScrewId) => (typeof bag[id] === 'number' ? num(bag[id]) : '')
        lines.push(
          [
            s.started,
            s.ended ?? '',
            s.kind === 'quick' ? 'quick' : 'session',
            m.time,
            screwTimeLocal(m.time),
            m.kind,
            m.corner ?? '',
            m.twice ? '1' : '0',
            String(index + 1),
            mm('FL'),
            mm('FR'),
            mm('RR'),
            mm('RL'),
            um('FL'),
            um('FR'),
            um('RR'),
            um('RL'),
          ].join(sep)
        )
      })
    }
  }
  return `${lines.join('\n')}\n`
}

// A quick measurement of one corner measured front left (the reference) and that corner only. The other corners
// keep their last values, moved by what front left moved (a new homing and cleaning may shift the whole frame),
// so every difference against front left stays the one of the last full measurement. Null without both results.
export function mergeCornerMeasure(
  base: Record<string, number>,
  text: string,
  corner: ScrewId
): { values: Record<string, number>; shift: number } | null {
  const results = cornerHeights(text)
  const key = SCREW_CORNERS.find(c => c.id === corner)?.key
  const flKey = SCREW_KEYS[0]
  if (!key || typeof results[flKey] !== 'number' || (key !== flKey && typeof results[key] !== 'number')) return null
  if (SCREW_KEYS.some(k => typeof base[k] !== 'number')) return null
  const shift = results[flKey] - base[flKey]
  const values: Record<string, number> = {}
  for (const k of SCREW_KEYS) values[k] = k === flKey ? results[flKey] : k === key ? results[k] : base[k] + shift
  return { values, shift }
}
