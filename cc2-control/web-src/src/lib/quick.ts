import { store } from './store'

// Dashboard shortcuts: four slots, each one of these actions. Saved on the printer with the other UI preferences.
export const QUICK_CHOICES: Record<string, [label: string, icon: string]> = {
  'home:ALL': ['Home All', 'home'],
  'home:X': ['Home X', 'home'],
  'home:Y': ['Home Y', 'home'],
  'home:Z': ['Home Z', 'home'],
  'system:heaters_off': ['All Heaters Off', 'temp'],
  'system:fans_off': ['Fans Off', 'fan'],
  'system:motors_off': ['Motors Off', 'motors'],
  'light:toggle': ['Lights', 'light'],
  'page:control': ['Control', 'control'],
  'page:files': ['Files', 'folder'],
  'page:bed': ['Bed Levelling', 'grid'],
  'page:canvas': ['Canvas', 'canvas'],
}
export const QUICK_DEFAULTS = ['home:ALL', 'system:heaters_off', 'system:fans_off', 'system:motors_off']
export const QUICK_ASK: Record<string, string> = {
  'home:ALL': 'Home all axes?',
  'home:X': 'Home X axis?',
  'home:Y': 'Home Y axis?',
  'home:Z': 'Home Z axis?',
  'system:heaters_off': 'Turn all heaters off?',
  'system:fans_off': 'Turn all fans off?',
  'system:motors_off': 'Disable all motors?',
}
// Navigation, lights and "heaters off" stay usable while the printer is busy; the rest need an idle printer.
export const quickAlwaysAvailable = (action: string) =>
  action.startsWith('page:') || action === 'system:heaters_off' || action === 'light:toggle'

export const quick = store({ actions: QUICK_DEFAULTS })

export const setQuickFromServer = (list: unknown) => {
  if (Array.isArray(list) && list.length === 4 && list.every(a => a in QUICK_CHOICES)) quick.set({ actions: list })
}

export async function saveQuickActions(actions: string[]) {
  quick.set({ actions })
  const body = Object.fromEntries(actions.map((a, i) => [`quick${i + 1}`, a]))
  await fetch('/api/preferences', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
}
