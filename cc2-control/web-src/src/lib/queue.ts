import { store } from './store'
import { post, request } from './api'

// The print queue: jobs prepared ahead (file, the print dialog's choices, copies). Every copy is a
// protected print start on the backend; the browser only asks for it. See cc2-control/docs/API.md.
// A filament as a Canvas slot labels it: the material type ("PLA"), the product line ("PLA Matte") and the
// colour ("#RRGGBB"). A type or a name left empty is not stated.
export type FilamentLabel = { type: string; name: string; color: string }
// One tool of a job that prints from the Canvas. The job is bound to the filament, not to a slot: the label
// is empty until the Canvas has labelled the slot the job was added with. `slot` is that slot (null: the job
// was added for a filament alone, which need not be in the printer) and `use` the slot a start would take
// now: the one that holds the filament, or null when none does (the backend then refuses the start).
// `reason`: 'empty' or 'other' is what the slot the job was added with holds now, 'absent' that no slot
// holds the filament, 'unknown' that the Canvas has reported no slots to look in.
export type QueueFilament = FilamentLabel & {
  tool: number
  slot: number | null
  use: number | null
  reason?: '' | 'empty' | 'other' | 'absent' | 'unknown'
}
export type QueueItem = {
  id: number
  storage: 'internal' | 'usb'
  path: string
  mapping: string // "tool:slot,...": the slots the job was added with
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
  last_result: '' | 'stopped' | 'not_started' | 'auto_failed' | 'auto_material'
  present: boolean
  filaments?: QueueFilament[] // none: the external feed
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
  auto_in: number | null // seconds until a pending automatic start
  items: QueueItem[]
}

export const queue = store({ data: null as Queue | null, ok: true })

export async function refreshQueue() {
  try {
    // A backend without the queue (or a demo that does not simulate it) may answer 200 with another
    // shape: only a reply with an item list is a queue.
    const data = await request('/api/queue')
    queue.set({ data: Array.isArray(data?.items) ? (data as Queue) : null, ok: true })
  } catch {
    queue.set({ ok: false })
  }
}

const items = (q: Queue | null) => (Array.isArray(q?.items) ? q.items : [])
export const queueItem = (q: Queue | null, id: number | null | undefined) =>
  id ? items(q).find(i => i.id === id) || null : null
export const queueNext = (q: Queue | null) => queueItem(q, q?.next)
export const queueActive = (q: Queue | null) => queueItem(q, q?.active)
export const copiesLeft = (i: QueueItem) => Math.max(0, i.copies - i.done)
export const openJobs = (q: Queue | null) => items(q).filter(i => i.state !== 'done')
export const finishedJobs = (q: Queue | null) => items(q).filter(i => i.state === 'done')

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

// The job's filaments with the slot a start would take for each, as the backend resolved them; a reply
// without them means the slots the job was added with.
export const jobFilaments = (i: QueueItem): QueueFilament[] =>
  Array.isArray(i.filaments)
    ? i.filaments
    : mappingSlots(i.mapping).map(s => ({ ...s, type: '', name: '', color: '', use: s.slot }))
export const filamentKnown = (f: FilamentLabel) => Boolean((f.type || f.name) && f.color)
export const filamentName = (f: FilamentLabel) => f.name || f.type
// The filaments a start has no slot for, one entry per filament (tools may share one): the job cannot start.
export const missingFilaments = (i: QueueItem) => {
  const seen = new Set<string>()
  return jobFilaments(i).filter(f => {
    const key = [f.type, f.name, f.color, f.slot, f.reason].join('|')
    return f.use === null && !seen.has(key) && seen.add(key)
  })
}
export const startBlocked = (i: QueueItem) => jobFilaments(i).some(f => f.use === null)

// Copies still queued for a file: the Files page marks it.
export const queuedCopiesFor = (q: Queue | null, storage: string, path: string) =>
  items(q)
    .filter(i => i.storage === storage && i.path === path)
    .reduce((n, i) => n + copiesLeft(i), 0)

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
  // Tools bound to a filament by itself: `mapping` names the slots of the others.
  filaments?: ToolFilament[]
}
export type ToolFilament = FilamentLabel & { tool: number }
// One "tool<TAB>type<TAB>name<TAB>#RRGGBB" line per tool bound to a filament by itself.
const filamentLines = (list: ToolFilament[] = []) => list.map(f => [f.tool, f.type, f.name, f.color].join('\t'))
// The same eight lines as /api/gcode-files/print, then copies, position, the slicer's estimates and the
// filaments of the tools that have no slot.
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
      ...filamentLines(a.filaments),
    ].join('\n')
  )
// Other filaments for tools of a job: `mapping` for the tools that take what a slot holds now, `filaments`
// for the ones bound to a filament by itself. The tools named in neither keep theirs.
export const setQueuedFilaments = (id: number, mapping: string, filaments: ToolFilament[] = []) =>
  post('/api/queue/filament', [String(id), mapping, ...filamentLines(filaments)].join('\n'))
export const startQueued = (id?: number) => post('/api/queue/start', id ? String(id) : '')
export const removeQueued = (id: number) => post('/api/queue/remove', String(id))
export const reorderQueued = (id: number, index: number) => post('/api/queue/reorder', `${id}\n${index}`)
export const setQueuedCopies = (id: number, copies: number) => post('/api/queue/copies', `${id}\n${copies}`)
export const setQueuePolicy = (p: QueuePolicy) =>
  post('/api/queue/policy', `${p.after}\n${p.delay}\n${p.remove_done ? 1 : 0}`)
export const clearFinished = () => post('/api/queue/clear-done')
// Cancels a pending automatic start; the policy stays as it is.
export const holdQueue = () => post('/api/queue/hold')
// Gives up a start the printer never ran (only while it is idle): the job goes back to pending.
export const abandonQueued = (id?: number) => post('/api/queue/abandon', id ? String(id) : '')

// The picture a slicer embedded in a job's G-code, as an object URL ('' when the file has none). The printer
// reads up to 4 MB of a file to find it, so a job is asked for once per page load and the requests go one
// at a time. Only "no picture" is remembered: a request that failed is made again when it is next needed.
const pictures = new Map<number, { file: string; made: Promise<string> }>()
let lastPicture: Promise<unknown> = Promise.resolve()
export const jobPicture = (item: QueueItem): Promise<string> => {
  const file = `${item.storage}\n${item.path}`
  const known = pictures.get(item.id)
  if (known?.file === file) return known.made
  const made = lastPicture.then(async () => {
    try {
      const r = await fetch('/api/gcode-files/thumbnail', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain;charset=UTF-8', 'X-CC2-Request': '1' },
        body: file,
      })
      const blob = await r.blob()
      if (r.ok && blob.type.startsWith('image/')) return URL.createObjectURL(blob)
      if (r.status !== 404) pictures.delete(item.id)
    } catch {
      pictures.delete(item.id)
    }
    return ''
  })
  lastPicture = made
  pictures.set(item.id, { file, made })
  return made
}
