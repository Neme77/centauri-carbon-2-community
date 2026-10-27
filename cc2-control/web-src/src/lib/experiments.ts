import { request } from './api'
import { store } from './store'

export type ExperimentState = {
  schema: number
  editable: boolean
  ready: boolean
  canvas_uds: boolean
  canvas_uds_available: boolean
  external_feed?: boolean
  bed_drying?: boolean
  quiet_motors: boolean
  normal_confirmed: boolean
  cli_canvas: boolean
}
export const experimental = store({ data: null as ExperimentState | null, ok: false })
let pending: Promise<void> | null = null
export function refreshExperiments() {
  if (!pending)
    pending = request('/api/experiments')
      .then((data: ExperimentState) => {
        experimental.set({ data, ok: data?.schema === 1 })
      })
      .catch(() => experimental.set({ data: experimental.get().data, ok: false }))
      .finally(() => {
        pending = null
      })
  return pending
}
