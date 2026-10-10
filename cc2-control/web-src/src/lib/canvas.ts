import { store } from './store'
import { request } from './api'

export function canvasModel(data: any) {
  const telemetry = data?.telemetry || data
  const list = telemetry?.result?.canvas_info?.canvas_list
  if (!Array.isArray(list) || !list.length) return null
  const m = list.find((i: any) => Number(i.connected) === 1) || list[0]
  return {
    connected: Number(m.connected) === 1,
    trays: Array.from({ length: 4 }, (_, slot) =>
      Array.isArray(m.tray_list) ? m.tray_list.find((tray: any) => Number(tray.tray_id) === slot) : undefined
    ) as any[],
  }
}

export function canvasColour(value: any, index: number): string {
  if (Array.isArray(value) && value.length >= 3)
    return `rgb(${value
      .slice(0, 3)
      .map(p => Math.max(0, Math.min(255, Number(p) || 0)))
      .join(',')})`
  if (typeof value === 'number') return `#${(value >>> 0).toString(16).slice(-6).padStart(6, '0')}`
  if (typeof value === 'string') {
    const c = value.trim()
    if (/^0x[0-9a-f]{6,8}$/i.test(c)) return `#${c.slice(2, 8)}`
    if (/^#?[0-9a-f]{8}$/i.test(c)) return `#${c.replace('#', '').slice(0, 6)}`
    if (/^#?[0-9a-f]{6}$/i.test(c)) return c.startsWith('#') ? c : `#${c}`
    if (/^rgb|^[a-z]+$/i.test(c)) return c
  }
  return ['#ef5350', '#42a5f5', '#fdd835', '#66bb6a'][index % 4]
}

export function canvasHex(value: any, index: number) {
  const c = canvasColour(value, index)
  const hex = c.match(/^#([0-9a-f]{6})$/i)
  if (hex) return `#${hex[1].toUpperCase()}`
  const rgb = c.match(/^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/i)
  return rgb
    ? `#${rgb
        .slice(1)
        .map(p => Number(p).toString(16).padStart(2, '0'))
        .join('')
        .toUpperCase()}`
    : '#00CFE8'
}

export const canvas = store({
  model: null as ReturnType<typeof canvasModel>,
  autoRefill: null as boolean | null, // null until the printer has reported the setting
  slot: 0,
  checked: '',
  eject: { available: false, running: false, slot: -1, result: 'unavailable', travel: 0 },
  optimistic: {} as Record<number, { colour: string; material: string; until: number }>,
})

export async function refreshCanvas() {
  try {
    const [data, eject] = await Promise.all([request('/api/canvas'), request('/api/canvas/eject').catch(() => null)])
    canvas.set({
      model: canvasModel(data),
      autoRefill: typeof data?.auto_refill === 'boolean' ? data.auto_refill : null,
      checked: new Date().toLocaleTimeString(),
      eject: eject || { available: false, running: false, slot: -1, result: 'unavailable', travel: 0 },
    })
  } catch {
    /* keep last */
  }
}
