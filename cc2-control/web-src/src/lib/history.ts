import { store } from './store'
import { post, request } from './api'

// One print task as the printer lists it (vendor method 1036, relayed by /api/history).
export type Task = {
  id: string
  name: string
  begin: number
  end: number
  status: number // 1 completed, 2 and 3 stopped, 4 printing, 5 paused, as ElegooSlicer's LAN view labels them
  video: number // time-lapse: 0 not recorded, 1 frames not rendered yet, 2 MP4 ready, 3 rendering failed
  size: number
}

export const history = store({
  tasks: [] as Task[],
  loaded: false,
  available: false,
  pending: false,
  generating: false,
  error: -1, // printer error_code of the last reply; -2: too large for CC2 Control
})

const num = (v: any) => (Number.isFinite(Number(v)) ? Number(v) : 0)

// Reads what CC2 Control holds; it never asks the printer by itself.
export async function refreshHistory() {
  try {
    const d = await request('/api/history')
    const list = d?.reply?.result?.history_task_list
    const tasks: Task[] = (Array.isArray(list) ? list : [])
      .filter((x: any) => x && typeof x.task_id === 'string')
      .map((x: any) => ({
        id: x.task_id,
        name: String(x.task_name || ''),
        begin: num(x.begin_time),
        end: num(x.end_time),
        status: num(x.task_status),
        video: num(x.time_lapse_video_status),
        size: num(x.time_lapse_video_size),
      }))
      .sort((a, b) => b.begin - a.begin)
    history.set({
      tasks,
      loaded: true,
      available: Boolean(d?.available),
      pending: Boolean(d?.pending),
      generating: Boolean(d?.generating),
      error: Number.isFinite(Number(d?.error_code)) ? Number(d.error_code) : -1,
    })
  } catch {
    history.set({ loaded: true })
  }
}

// Asks the printer for its list once; the reply reaches /api/history a moment later.
export const requestHistory = () => post('/api/history/refresh')
export const renderTimelapse = (id: string) => post('/api/history/timelapse', id)
export const timelapseUrl = (id: string) => `/api/history/timelapse?task=${encodeURIComponent(id)}`
