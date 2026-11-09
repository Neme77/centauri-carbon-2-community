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
