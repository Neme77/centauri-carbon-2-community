import type { Key } from './i18n'
import { store } from './store'
import { request } from './api'

// Dashboard shortcuts: four slots, each one of these actions. Saved on the printer with the other UI preferences.
export const QUICK_CHOICES: Record<string, [label: Key, icon: string]> = {
  'home:ALL': ['common.home_all', 'home'],
  'home:X': ['common.home_x', 'home'],
  'home:Y': ['common.home_y', 'home'],
  'home:Z': ['common.home_z', 'home'],
  'system:heaters_off': ['common.all_heaters_off', 'temp'],
  'system:fans_off': ['common.fans_off', 'fan'],
  'system:motors_off': ['common.motors_off', 'motors'],
  'light:toggle': ['common.lights', 'light'],
  'page:control': ['common.control', 'control'],
  'page:files': ['common.files', 'folder'],
  'page:bed': ['common.bed_levelling', 'grid'],
  'page:canvas': ['common.canvas', 'canvas'],
}
export const QUICK_DEFAULTS = ['home:ALL', 'system:heaters_off', 'system:fans_off', 'system:motors_off']
export const QUICK_ASK: Record<string, Key> = {
  'home:ALL': 'common.home_all_axes',
  'home:X': 'common.home_x_axis',
  'home:Y': 'common.home_y_axis',
  'home:Z': 'common.home_z_axis',
  'system:heaters_off': 'common.turn_all_heaters_off',
  'system:fans_off': 'common.turn_all_fans_off',
  'system:motors_off': 'common.disable_all_motors',
}
// Navigation and lights stay usable while the printer is busy; the rest need an idle printer.
export const quickAlwaysAvailable = (action: string) => action.startsWith('page:') || action === 'light:toggle'

export const quick = store({ actions: QUICK_DEFAULTS })

export const setQuickFromServer = (list: unknown) => {
  if (Array.isArray(list) && list.length === 4 && list.every(a => a in QUICK_CHOICES)) quick.set({ actions: list })
}

export async function saveQuickActions(actions: string[]) {
  const body = Object.fromEntries(actions.map((a, i) => [`quick${i + 1}`, a]))
  await request('/api/preferences', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json', 'X-CC2-Request': '1' },
    body: JSON.stringify(body),
  })
  quick.set({ actions })
}
