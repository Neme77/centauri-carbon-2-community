export const SCREW_SETTLE_S = 30
export const SCREW_KEYS = ['30,30', '230,30', '230,225', '30,225'] as const
export const SCREW_CORNERS = [
  { id: 'FL', key: '30,30', x: 30, y: 30, n: '1' },
  { id: 'FR', key: '230,30', x: 230, y: 30, n: '2' },
  { id: 'RR', key: '230,225', x: 230, y: 225, n: '3' },
  { id: 'RL', key: '30,225', x: 30, y: 225, n: '4' },
] as const
export type ScrewId = (typeof SCREW_CORNERS)[number]['id']

const isScrewKey = (k: string): k is (typeof SCREW_KEYS)[number] => (SCREW_KEYS as readonly string[]).includes(k)

const keyOf = (x: number, y: number) => `${Math.round(x)},${Math.round(y)}`

// Median of the last three probes at each of the four screw positions, parsed from console output.
export function screwSamples(text: string): Record<string, number> {
  const re = /probe at\s+(-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?)\s+is z=(-?\d+(?:\.\d+)?)/gi,
    groups = new Map<string, number[]>()
  for (const m of (text || '').matchAll(re)) {
    const k = keyOf(+m[1], +m[2])
    if (!isScrewKey(k)) continue
    if (!groups.has(k)) groups.set(k, [])
    groups.get(k)?.push(+m[3])
  }
  const out: Record<string, number> = {}
  for (const [k, v] of groups) if (v.length >= 3) out[k] = [...v.slice(-3)].sort((a, b) => a - b)[1]
  return out
}

export function screwValues(text: string): Record<string, number> | null {
  const out = screwSamples(text)
  return Object.keys(out).length === 4 ? out : null
}

export type ProbeResult = { key: string; z: number }

// One entry per PROBE command, using the last in-range "probe at" coordinates before "Result is z=".
export function probeResults(text: string): ProbeResult[] {
  const out: ProbeResult[] = []
  let lastKey = ''
  for (const line of (text || '').split(/\r?\n/)) {
    const at = /probe at\s+(-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?)\s+is z=/i.exec(line)
    if (at) {
      const k = keyOf(+at[1], +at[2])
      if (isScrewKey(k)) lastKey = k
    }
    const result = /Result is z=(-?\d+(?:\.\d+)?)/i.exec(line)
    if (result && lastKey) out.push({ key: lastKey, z: +result[1] })
  }
  return out
}

export function cornerHeights(text: string): Record<string, number> {
  const results = probeResults(text)
  if (results.length) {
    const out: Record<string, number> = {}
    for (const r of results) out[r.key] = r.z
    return out
  }
  return screwSamples(text)
}

export function probePair(text: string, key: string): { first: number; second: number } | null {
  const hits = probeResults(text).filter(r => r.key === key)
  if (hits.length >= 2) return { first: hits[hits.length - 2].z, second: hits[hits.length - 1].z }
  return null
}

export function screwPlan(values: Record<string, number>) {
  const ref = values[SCREW_KEYS[0]],
    standard = SCREW_KEYS.map(k => values[k] - ref),
    other = standard.slice(1)
  const sameSign = other.every(v => v > 0.02) || other.every(v => v < -0.02)
  const common = sameSign ? Math.sign(other[0]) * Math.min(...other.map(Math.abs)) : 0
  const optimized = [common, ...other.map(v => v - common)]
  const peak = (l: number[]) => Math.max(...l.map(Math.abs))
  const useOptimized = sameSign && Math.abs(common) <= 0.5 && peak(optimized) < peak(standard) - 0.005
  return {
    shown: useOptimized ? optimized : standard,
    useOptimized,
    common,
    before: Math.round(peak(standard) * 1000),
    best: Math.round(peak(optimized) * 1000),
  }
}
export const microns = (v: number) => `${v > 0 ? '+' : v < 0 ? '−' : ''}${Math.abs(Math.round(v * 1000))} µm`

export type ScrewHistoryMeasurement = {
  time: number
  kind: 'start' | 'probe' | 'check' | 'quick'
  corner: ScrewId | null
  twice: boolean
  z: Partial<Record<ScrewId, number>>
  z2?: Partial<Record<ScrewId, number>>
}

export type ScrewHistoryEntryKind = 'session' | 'quick'

export type ScrewHistorySession = {
  // Missing on an older backend. Callers treat that as a session.
  kind: ScrewHistoryEntryKind
  started: number
  ended: number | null
  measurements: ScrewHistoryMeasurement[]
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
        })
      }
    }
    sessions.push({
      kind,
      started: typeof s.started === 'number' ? s.started : 0,
      ended: typeof s.ended === 'number' ? s.ended : null,
      measurements,
    })
  }
  return sessions
}

// Offsets against the front-left raw Z, in millimetres, oldest first.
// A single-corner probe keeps the last front-left value. Repeated identical micrometres are dropped.
export function cornerOffsets(session: ScrewHistorySession): Record<ScrewId, number[]> {
  const raw: Partial<Record<ScrewId, number>> = {}
  const series: Record<ScrewId, number[]> = { FL: [], FR: [], RR: [], RL: [] }
  for (const m of session.measurements) {
    if (m.kind === 'quick') continue
    const bags = [m.z, ...(m.twice && m.z2 ? [m.z2] : [])]
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

const signedUm = (v: number) => `${v > 0 ? '+' : v < 0 ? '−' : ''}${Math.abs(Math.round(v * 1000))}`
const umLabel = (um: number) => `${um > 0 ? '+' : um < 0 ? '−' : ''}${Math.abs(um)}`

export type ReferenceMove = {
  // "0 → +2 µm": the latest front-left z against the first z of this session. Twice uses z, not z2.
  change: string
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
    const fl = m.z.FL
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
  return { change: `0 → ${umLabel(changeUm)} µm`, sinceFullUm: since }
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
  const fl = m.z.FL
  const out: Record<ScrewId, number | null> = { FL: null, FR: null, RR: null, RL: null }
  if (typeof fl !== 'number') return out
  for (const id of SCREW_IDS) {
    const z = m.z[id]
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
