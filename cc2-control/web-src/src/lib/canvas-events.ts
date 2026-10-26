import { store } from './store'

// These are observations of CC2's existing Canvas slot readbacks. They are
// NOT confirmations of physical filament movement, purge success or failure.
export type ObservedState = 'empty' | 'present' | 'loaded' | 'transition'
export type ObservedPrinterPhase = 'idle' | 'printing' | 'paused' | 'unknown'
export type CanvasObservation = {
  id: number
  time: number
  slot: number
  from: ObservedState
  to: ObservedState
  phase: ObservedPrinterPhase
}

const LIMIT = 24
let previous: (ObservedState | null)[] | null = null
let sequence = 0

const normalize = (value: unknown): ObservedState | null =>
  value === 'empty' || value === 'present' || value === 'loaded' || value === 'transition' ? value : null

export const canvasTimeline = store({ events: [] as CanvasObservation[] })

// A phase is only a recent printer readback. Never infer the print state
// from Canvas slot changes, and never label an old/offline sample as current.
// The cached printer timestamp is performance.now(), not Date.now().
export function observedPrinterPhase(reading: unknown, now = performance.now()): ObservedPrinterPhase {
  if (!reading || typeof reading !== 'object' || Array.isArray(reading)) return 'unknown'
  const r = reading as { ok?: unknown; at?: unknown; data?: unknown }
  if (r.ok !== true || typeof r.at !== 'number' || !Number.isFinite(r.at) || now < r.at || now - r.at > 5000)
    return 'unknown'
  const data = r.data as { connected?: unknown; machine?: { status?: unknown; sub_status?: unknown } } | null
  if (!data || typeof data !== 'object' || data.connected !== true) return 'unknown'
  if (data.machine?.status === 1) return 'idle'
  if (data.machine?.status === 2)
    return data.machine.sub_status === 2502 || data.machine.sub_status === 2505 ? 'paused' : 'printing'
  return 'unknown'
}

export function observeCanvasSlots(
  connected: boolean,
  controls: unknown,
  now = Date.now(),
  phase: ObservedPrinterPhase = 'unknown'
) {
  // A stale/disconnected/unrecognized readback invalidates our comparison
  // baseline. Never invent a slot change after telemetry was unavailable.
  if (!connected || !Array.isArray(controls)) {
    previous = null
    return
  }
  const rows = controls.filter(row => row && typeof row === 'object' && !Array.isArray(row))
  const current = Array.from({ length: 4 }, (_, slot) => {
    const matching = rows.filter(item => (item as { slot?: unknown }).slot === slot)
    const row = matching.length === 1 ? (matching[0] as { state?: unknown }) : undefined
    return normalize(row?.state)
  })
  if (previous) {
    const changes: CanvasObservation[] = []
    for (let slot = 0; slot < 4; slot++) {
      const from = previous[slot]
      const to = current[slot]
      if (from && to && from !== to) changes.push({ id: ++sequence, time: now, slot, from, to, phase })
    }
    if (changes.length) {
      canvasTimeline.set(({ events }) => ({ events: [...changes.reverse(), ...events].slice(0, LIMIT) }))
    }
  }
  previous = current
}

export function clearCanvasTimeline() {
  canvasTimeline.set({ events: [] })
}
