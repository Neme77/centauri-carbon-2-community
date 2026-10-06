import { store } from './store'
import { request } from './api'
import { printer, view } from './state'

export const objects = store({
  list: [] as string[],
  excluded: [] as string[],
  current: '',
  has: false,
  error: false,
  selected: '',
})

const model = (d: any) =>
  (d?.result && (d.result.status?.exclude_object || d.result.exclude_object)) || d?.status?.exclude_object || null

export async function refreshObjects() {
  // Objects exist only during a print, and every printer request this route might
  // make stays in the firmware's memory: an idle Job page asks for nothing.
  if (!view(printer.get().data).active) return
  try {
    const m = model(await request('/api/exclude-objects'))
    const has = Boolean(m && Array.isArray(m.objects))
    const list = has ? m.objects.map((o: any) => String(o?.name !== undefined ? o.name : o)) : []
    const excluded = m && Array.isArray(m.excluded_objects) ? m.excluded_objects.map(String) : []
    const current = m?.current_object !== undefined && m.current_object !== null ? String(m.current_object) : ''
    objects.set(s => ({
      list,
      excluded,
      current,
      has,
      error: false,
      selected: list.includes(s.selected) && !excluded.includes(s.selected) ? s.selected : '',
    }))
  } catch {
    objects.set({ list: [], excluded: [], current: '', has: false, error: true, selected: '' })
  }
}
export const activePrint = () => view(printer.get().data).active
