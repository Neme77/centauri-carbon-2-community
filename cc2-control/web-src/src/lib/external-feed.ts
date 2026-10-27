import { request } from './api'
import { store } from './store'
export type ExternalFeed = {
  available: boolean
  idle: boolean
  experiment?: boolean
  source?: 'canvas' | 'external'
  can_select?: boolean
  can_unload?: boolean
  temperature?: number
  filament: any
}
export const externalFeed = store({ available: false, idle: false, filament: null } as ExternalFeed)
let pending: Promise<void> | null = null
export function refreshExternalFeed() {
  if (!pending)
    pending = request('/api/external-filament')
      .then((state: ExternalFeed) => externalFeed.set(state))
      .catch(() => externalFeed.set({ idle: false, can_select: false, can_unload: false }))
      .finally(() => {
        pending = null
      })
  return pending
}
