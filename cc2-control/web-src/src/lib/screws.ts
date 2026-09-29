export const SCREW_KEYS = ['35,30', '225,30', '225,225', '35,225'] as const

// Median of the last three probes at each of the four screw positions, parsed from console output.
export function screwValues(text: string): Record<string, number> | null {
  const re = /probe at\s+(-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?)\s+is z=(-?\d+(?:\.\d+)?)/gi, groups = new Map<string, number[]>()
  for (const m of (text || '').matchAll(re)) {
    const k = `${Math.round(+m[1])},${Math.round(+m[2])}`
    if (!(SCREW_KEYS as readonly string[]).includes(k)) continue
    if (!groups.has(k)) groups.set(k, [])
    groups.get(k)?.push(+m[3])
  }
  const out: Record<string, number> = {}
  for (const [k, v] of groups) if (v.length >= 3) out[k] = [...v.slice(-3)].sort((a, b) => a - b)[1]
  return Object.keys(out).length === 4 ? out : null
}

export function screwPlan(values: Record<string, number>) {
  const ref = values[SCREW_KEYS[0]], standard = SCREW_KEYS.map(k => values[k] - ref), other = standard.slice(1)
  const sameSign = other.every(v => v > 0.02) || other.every(v => v < -0.02)
  const common = sameSign ? Math.sign(other[0]) * Math.min(...other.map(Math.abs)) : 0
  const optimized = [common, ...other.map(v => v - common)]
  const peak = (l: number[]) => Math.max(...l.map(Math.abs))
  const useOptimized = sameSign && Math.abs(common) <= 0.5 && peak(optimized) < peak(standard) - 0.005
  return { shown: useOptimized ? optimized : standard, useOptimized, common, before: Math.round(peak(standard) * 1000), best: Math.round(peak(optimized) * 1000) }
}
export const microns = (v: number) => `${v > 0 ? '+' : v < 0 ? '−' : ''}${Math.abs(Math.round(v * 1000))} µm`
