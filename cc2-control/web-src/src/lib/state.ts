import { store, ls } from './store'
import { request } from './api'
import { duration, finishTime } from './format'
import { subState } from './machine'

export type Page = 'dashboard' | 'control' | 'job' | 'files' | 'history' | 'bed' | 'canvas' | 'console' | 'settings'
const PAGES: Page[] = ['dashboard', 'control', 'job', 'files', 'history', 'bed', 'canvas', 'console', 'settings']

// Routes live in the URL hash (#files, #bed/plates, #bed/screws): the backend only serves /, so no server change is needed.
// #camera is the separate camera window opened from the camera card; it is not a menu page.
export type BedTab = 'mesh' | 'plates' | 'screws'
const route = () => {
  const [p, sub] = location.hash.slice(1).split('/')
  return {
    page: (PAGES.includes(p as Page) ? p : 'dashboard') as Page,
    bed: (p === 'bed' && (sub === 'plates' || sub === 'screws') ? sub : 'mesh') as BedTab,
    camera: p === 'camera',
  }
}
export const nav = store(route())
addEventListener('hashchange', () => {
  nav.set(route())
  window.scrollTo(0, 0)
})
export const openPage = (page: Page, bed: BedTab = 'mesh') => {
  const hash = `#${page}${page === 'bed' && bed !== 'mesh' ? `/${bed}` : ''}`
  if (location.hash === hash) nav.set({ page, bed, camera: false })
  else location.hash = hash
}

// Side menu: collapsed to icons only; the choice is remembered per browser.
export const mobileMenu = store({ open: false })
export const menu = store({ collapsed: ls.get('cc2-menu') === 'collapsed' })
export const toggleMenu = () => {
  const collapsed = !menu.get().collapsed
  ls.set('cc2-menu', collapsed ? 'collapsed' : 'open')
  menu.set({ collapsed })
}

// `at`: performance.now() when the request behind `data` was sent, so a reply predating a change can be told apart.
export const printer = store({ data: null as any, rev: 0, ok: true, at: 0 })
export const health = store({ data: null as any, setup: null as any })
export const consoleLog = store({ text: null as string | null }) // null: nothing received yet, the page shows its own placeholder
export const screwText = store({ text: '', minGeneration: 0 })

export const thermalTimes = { nozzle: [] as number[], bed: [] as number[], chamber: [] as number[] }
export const thermalHistory = { nozzle: [] as number[], bed: [] as number[], chamber: [] as number[] }

export async function refreshPrinter() {
  const sent = performance.now()
  try {
    const data = await request('/api/printer')
    for (const [key, v] of [
      ['nozzle', data.extruder?.temperature],
      ['bed', data.heater_bed?.temperature],
      ['chamber', data.chamber?.temperature],
    ] as const) {
      if (v !== null && v !== undefined && Number.isFinite(Number(v))) {
        const a = thermalHistory[key]
        a.push(Number(v))
        const times = thermalTimes[key]
        times.push(Date.now())
        while (times.length && times[0] < Date.now() - 300_000) {
          times.shift()
          a.shift()
        }
      }
    }
    zoffset.set({
      v: typeof data.z_offset?.value === 'number' ? data.z_offset.value : null,
      pending: Boolean(data.z_offset?.pending),
      timedOut: Boolean(data.z_offset?.timed_out),
      reference: typeof data.z_offset?.reference === 'number' ? data.z_offset.reference : null,
    })
    printer.set(s => ({ data, rev: s.rev + 1, ok: true, at: sent }))
  } catch {
    zoffset.set({ v: null })
    printer.set({ ok: false })
  } // keep the last data, flag the link as down
}

export async function refreshHealth() {
  try {
    health.set({ data: await request('/api/health') })
  } catch {
    /* offline */
  }
}

// Only Settings > Connection shows the LAN-code state, so only that page polls it.
export async function refreshSetup() {
  try {
    health.set({ setup: await request('/api/setup') })
  } catch {
    /* offline */
  }
}

export async function refreshConsole() {
  try {
    const status = await request('/api/console')
    const out = String(status.output || '')
    if (status.generation >= screwText.get().minGeneration) {
      const measured = String(status.command || '').includes('SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE')
      screwText.set({ text: measured && status.completed && status.success ? out : '' })
    }
    if ((out || null) !== consoleLog.get().text) {
      consoleLog.set({ text: out || null })
    }
  } catch {
    /* offline */
  }
}

// Derived, display-ready view of the /api/printer payload.
export function view(d: any) {
  const state: string = d?.machine?.status_name || 'Unknown'
  const jobState = String(d?.print?.state || '')
  const paused = /pause/i.test(jobState) || /pause/i.test(state)
  const rawFilename: string = d?.print?.filename || ''
  const layer = d?.print?.current_layer || 0
  const totalRaw = d?.print && (d.print.total_layer || d.print.total_layers || d.print.total_layer_count)
  const total: number | string = Number(totalRaw) > 0 ? Number(totalRaw) : '—'
  const active =
    Boolean(d?.print?.filename) && (/print|pause/i.test(state) || /print|pause/i.test(String(d?.print?.state || '')))
  const elapsed = Number(d?.print?.duration),
    remaining = Number(d?.print?.remaining)
  const homed = String(d?.motion?.homed_axes || '')
  return {
    state,
    detail: subState(d?.machine),
    rawFilename,
    layer,
    total,
    active,
    homed,
    progress: Math.max(0, Math.min(100, Number(d?.machine?.progress || 0))),
    idle: /idle|ready/i.test(state),
    printing: !paused && (/print/i.test(state) || /print/i.test(jobState)),
    paused,
    elapsedText: active ? duration(elapsed) : '—',
    remainingText: active ? duration(remaining) : '—',
    finishText: active ? finishTime(remaining) : '—',
    projected:
      active && Number.isFinite(elapsed) && Number.isFinite(remaining)
        ? elapsed + remaining
        : Number(d?.print?.total_duration),
    lightOn: Number(d?.hardware?.light) === 1,
    pos: (a: 'x' | 'y' | 'z') => `${Number(d?.motion?.[a] || 0).toFixed(2)} mm`,
    fan: (k: 'part' | 'aux' | 'box') => Math.max(0, Math.min(100, Math.round((Number(d?.fans?.[k] || 0) / 255) * 100))),
  }
}

export const presetDefaults = [
  { name: 'PLA', nozzle: 200, bed: 60, min: 190, max: 230 },
  { name: 'PETG', nozzle: 240, bed: 70, min: 220, max: 260 },
  { name: 'ABS', nozzle: 250, bed: 100, min: 230, max: 280 },
  { name: 'ASA', nozzle: 255, bed: 100, min: 240, max: 280 },
  { name: 'TPU', nozzle: 220, bed: 50, min: 200, max: 240 },
  { name: 'PA-CF', nozzle: 285, bed: 100, min: 260, max: 300 },
]
export const presets = store({ list: presetDefaults.map(p => ({ ...p })) as any[] })
export async function loadPresets() {
  try {
    const l = await request('/api/material-presets')
    if (Array.isArray(l) && l.length) presets.set({ list: l })
  } catch {
    /* defaults */
  }
}
export const savePresets = (list: any[]) =>
  request('/api/material-presets', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(list),
  })

// Camera and Moonraker live on other ports of the same host.
export const camera = (q = '') => `http://${location.hostname}:8080/${q}`

// Authoritative volatile printer offset; unavailable readback is never shown as zero.
export const zoffset = store({
  v: null as number | null,
  reference: null as number | null,
  pending: false,
  timedOut: false,
})
