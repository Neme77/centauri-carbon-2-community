import { store } from './store'
import { post, request } from './api'

// The print queue: jobs prepared ahead (file, the print dialog's choices, copies). Every copy is a
// protected print start on the backend; the browser only asks for it. See cc2-control/docs/API.md.
export type QueueItem = {
  id: number
  storage: 'internal' | 'usb'
  path: string
  mapping: string // "tool:slot,..." or "" for the external feed
  side: 'A' | 'B'
  leveling: 'saved' | 'calibrate' | 'adaptive' | 'full'
  timelapse: boolean
  measure: string
  nozzle: string
  copies: number
  done: number
  estimated_seconds: number | null
  filament_grams: number | null
  added: number
  state: 'pending' | 'starting' | 'printing' | 'done'
  last_result: '' | 'stopped' | 'not_started' | 'auto_failed'
  present: boolean
}
export type QueuePolicy = { after: 'wait' | 'auto'; delay: number; remove_done: boolean }
export type Queue = {
  available: boolean
  error: string
  revision: number
  printer_idle: boolean
  policy: QueuePolicy
  active: number | null
  next: number | null
  items: QueueItem[]
}

export const queue = store({ data: null as Queue | null, ok: true })

export async function refreshQueue() {
  try {
    queue.set({ data: await request('/api/queue'), ok: true })
  } catch {
    queue.set({ ok: false })
  }
}

export const queueItem = (q: Queue | null, id: number | null | undefined) =>
  q && id ? q.items.find(i => i.id === id) || null : null
export const queueNext = (q: Queue | null) => queueItem(q, q?.next)
export const queueActive = (q: Queue | null) => queueItem(q, q?.active)
export const copiesLeft = (i: QueueItem) => Math.max(0, i.copies - i.done)
export const openJobs = (q: Queue | null) => q?.items.filter(i => i.state !== 'done') || []
export const finishedJobs = (q: Queue | null) => q?.items.filter(i => i.state === 'done') || []

// Jobs and copies still to print, and their time when every job states an estimate.
export const queueTotals = (q: Queue | null) => {
  const jobs = openJobs(q)
  let copies = 0,
    seconds = 0,
    unknown = false
  for (const i of jobs) {
    const left = copiesLeft(i)
    copies += left
    if (i.estimated_seconds === null) unknown = unknown || left > 0
    else seconds += i.estimated_seconds * left
  }
  return { jobs: jobs.length, copies, seconds, unknown }
}

// "0:1,1:2" -> [{ tool: 0, slot: 1 }, ...]
export const mappingSlots = (mapping: string) =>
  mapping
    .split(',')
    .filter(Boolean)
    .map(pair => {
      const [tool, slot] = pair.split(':').map(Number)
      return { tool, slot }
    })

// Copies still queued for a file: the Files page marks it.
export const queuedCopiesFor = (q: Queue | null, storage: string, path: string) =>
  (q?.items || []).filter(i => i.storage === storage && i.path === path).reduce((n, i) => n + copiesLeft(i), 0)

export type QueueAdd = {
  storage: string
  path: string
  mapping: string
  side: 'A' | 'B'
  leveling: string
  timelapse: boolean
  measure: string
  nozzle: string
  copies: number
  position: 'end' | 'next'
  estimatedSeconds: number | null
  filamentGrams: number | null
}
// The same eight lines as /api/gcode-files/print, then copies, position and the slicer's estimates.
export const addToQueue = (a: QueueAdd) =>
  post(
    '/api/queue/add',
    [
      a.storage,
      a.path,
      a.mapping,
      a.side,
      a.leveling,
      a.timelapse ? '1' : '0',
      a.measure,
      a.nozzle,
      String(a.copies),
      a.position,
      a.estimatedSeconds === null ? '' : String(Math.round(a.estimatedSeconds)),
      a.filamentGrams === null ? '' : String(a.filamentGrams),
    ].join('\n')
  )
export const startQueued = (id?: number) => post('/api/queue/start', id ? String(id) : '')
export const removeQueued = (id: number) => post('/api/queue/remove', String(id))
export const reorderQueued = (id: number, index: number) => post('/api/queue/reorder', `${id}\n${index}`)
export const setQueuedCopies = (id: number, copies: number) => post('/api/queue/copies', `${id}\n${copies}`)
export const setQueuePolicy = (p: QueuePolicy) =>
  post('/api/queue/policy', `${p.after}\n${p.delay}\n${p.remove_done ? 1 : 0}`)
export const clearFinished = () => post('/api/queue/clear-done')
