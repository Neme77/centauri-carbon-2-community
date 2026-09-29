import { useEffect } from 'preact/hooks'

// One scheduler for every periodic request. The CC2 is small, so it never runs two requests of the
// same source at once, it sleeps while the browser tab is hidden, and it shares one timer.
//
// - a source is identified by its function: several callers asking for the same one share a single
//   task that runs at the fastest interval requested;
// - the next run is scheduled when the previous one has finished, never while it is still running;
// - `hidden: true` keeps a source running in a hidden tab (OrcaSlicer's embedded WebView can report
//   "hidden" while it is in fact being used).
type Task = { fn: () => unknown; intervals: number[]; hidden: boolean; next: number; busy: boolean }

const tasks = new Map<() => unknown, Task>()
let timer = 0

const eligible = (t: Task) => !t.busy && (t.hidden || !document.hidden)

function schedule() {
  clearTimeout(timer)
  let soonest = Number.POSITIVE_INFINITY
  for (const t of tasks.values()) if (eligible(t)) soonest = Math.min(soonest, t.next)
  if (soonest === Number.POSITIVE_INFINITY) return
  timer = window.setTimeout(tick, Math.max(0, soonest - Date.now()))
}

function tick() {
  const now = Date.now()
  for (const t of tasks.values()) {
    if (!eligible(t) || t.next > now) continue
    t.busy = true
    Promise.resolve()
      .then(t.fn)
      .catch(() => {})
      .finally(() => {
        t.busy = false
        t.next = Date.now() + Math.min(...t.intervals)
        schedule()
      })
  }
  schedule()
}

// Starts (or joins) the periodic run of `fn`, first run as soon as possible. Returns the stop function.
export function poll(fn: () => unknown, every: number, opts: { hidden?: boolean } = {}) {
  let task = tasks.get(fn)
  if (!task) {
    task = { fn, intervals: [], hidden: false, next: 0, busy: false }
    tasks.set(fn, task)
  }
  task.intervals.push(every)
  task.hidden ||= Boolean(opts.hidden)
  task.next = Math.min(task.next, Date.now())
  schedule()
  const joined = task
  return () => {
    joined.intervals.splice(joined.intervals.indexOf(every), 1)
    if (!joined.intervals.length) tasks.delete(fn)
    schedule()
  }
}

// Component helper: poll while the component is mounted.
export const usePoll = (fn: () => unknown, every: number) => {
  useEffect(() => poll(fn, every), [every])
}

// Coming back to a hidden tab refreshes everything straight away.
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) for (const t of tasks.values()) t.next = Math.min(t.next, Date.now())
  schedule()
})
