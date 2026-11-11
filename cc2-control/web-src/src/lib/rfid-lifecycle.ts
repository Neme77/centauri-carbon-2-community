import { request } from './api'
import { store } from './store'

type Lifecycle = {
  installed: boolean
  metadata_valid: boolean
  phase: number
  busy: boolean
  result?: number
  reboot_required: boolean
  recovery_required: boolean
  retry_required: boolean
  activation_requested: boolean
}
export const rfidLifecycle = store<{ data: Lifecycle | null }>({ data: null })

let pending: Promise<void> | null = null
export function refreshRfidLifecycle() {
  if (!pending)
    pending = request('/api/rfid/lifecycle')
      .then(data => rfidLifecycle.set({ data }))
      .catch(() => rfidLifecycle.set({ data: null }))
      .finally(() => {
        pending = null
      })
  return pending
}

export const rfidRebootRequired = (state: Lifecycle | null) =>
  Boolean(state?.installed && state.metadata_valid && state.reboot_required && !state.busy && !state.recovery_required)
