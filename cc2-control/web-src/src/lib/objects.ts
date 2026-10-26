import { store } from './store'
import { request } from './api'
import { printer, view } from './state'
import { layoutObjects, type PrintObject } from './object-layout'

export const objects = store({
  list: [] as string[],
  entries: [] as PrintObject[],
  excluded: [] as string[],
  current: '',
  has: false,
  error: false,
  selected: '',
})

const model = (d: any) =>
  (d?.result && (d.result.status?.exclude_object || d.result.exclude_object)) || d?.status?.exclude_object || null

function clearObjects() {
  const old = objects.get()
  if (old.has || old.error || old.list.length || old.selected)
    objects.set({ list: [], entries: [], excluded: [], current: '', has: false, error: false, selected: '' })
}

export async function refreshObjects() {
  // Objects exist only during a print, and every printer request this route might
  // make stays in the firmware's memory: an idle Job page asks for nothing.
  const job = view(printer.get().data)
  if (!job.active) return clearObjects()
  try {
    const m = model(await request('/api/exclude-objects'))
    const currentJob = view(printer.get().data)
    if (!currentJob.active) return clearObjects()
    if (currentJob.rawFilename !== job.rawFilename) return
    const has = Boolean(m && Array.isArray(m.objects))
    const entries = has ? layoutObjects(m.objects) : []
    const list = entries.map(o => o.id)
    const excluded = m && Array.isArray(m.excluded_objects) ? m.excluded_objects.map(String) : []
    const current = m?.current_object !== undefined && m.current_object !== null ? String(m.current_object) : ''
    objects.set(s => ({
      list,
      entries,
      excluded,
      current,
      has,
      error: false,
      selected: list.includes(s.selected) && !excluded.includes(s.selected) ? s.selected : '',
    }))
  } catch {
    const currentJob = view(printer.get().data)
    if (!currentJob.active) return clearObjects()
    if (currentJob.rawFilename !== job.rawFilename) return
    objects.set({ list: [], entries: [], excluded: [], current: '', has: false, error: true, selected: '' })
  }
}
export const activePrint = () => view(printer.get().data).active
