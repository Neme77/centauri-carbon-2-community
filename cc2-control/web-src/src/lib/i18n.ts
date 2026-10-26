import en from '../../public/locales/en.json'
import { store, ls } from './store'
import { notify, request } from './api'
import { setQuickFromServer } from './quick'

export const LANGUAGE_NAMES: Record<string, string> = {
  en: 'English',
  it: 'Italiano',
  fr: 'Français',
  zh: '中文',
  ru: 'Русский',
  pl: 'Polski',
  cs: 'Čeština',
  hu: 'Magyar',
  es: 'Español',
}

// Use displayed native names, a fixed collation and accent-insensitive ordering on every device.
export const LANGUAGE_OPTIONS = Object.entries(LANGUAGE_NAMES).sort(([, a], [, b]) =>
  a.localeCompare(b, 'en', { sensitivity: 'base' })
)

// The browser's first language we ship (fr-CA -> fr), else English.
export const detectLanguage = () =>
  navigator.languages?.map(l => l.slice(0, 2).toLowerCase()).find(l => l in LANGUAGE_NAMES) ?? 'en'

// Every UI string is an identifier from public/locales/en.json (the source language, bundled); other locales load on demand.
export type Key = keyof typeof en
const source: Record<string, string> = en
const sourceKeys = new Set(Object.keys(source))

export const i18n = store({ lang: ls.get('cc2-language') || detectLanguage(), dict: {} as Record<string, string> })
// Palettes are defined in index.css; the backend only stores the identifier (see preferences_theme in main.c).
export const THEMES: { id: string; key?: Key; label?: string }[] = [
  { id: 'dark', key: 'common.dark' },
  { id: 'light', key: 'common.light' },
  { id: 'dracula', label: 'Dracula' },
  { id: 'nord', label: 'Nord' },
  { id: 'monokai', label: 'Monokai' },
  { id: 'solarized-light', label: 'Solarized Light' },
]
const themeId = (mode: unknown) => (THEMES.some(x => x.id === mode) ? (mode as string) : 'dark')
export const theme = store({ mode: themeId(ls.get('cc2-theme')) })

export const cameraPreferences = store({ persistent: false })
export async function setPersistentCamera(persistent: boolean) {
  await request('/api/preferences', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json', 'X-CC2-Request': '1' },
    body: JSON.stringify({ camera_persistent: persistent }),
  })
  cameraPreferences.set({ persistent })
}

const cache: Record<string, Record<string, string>> = {}
const localeRequests = new Map<string, Promise<Record<string, string> | null>>()
async function boundedJson(path: string) {
  const controller = new AbortController()
  const timer = window.setTimeout(() => controller.abort(), 1800)
  try {
    const response = await fetch(path, { cache: 'no-store', signal: controller.signal })
    if (!response.ok) throw Error(`HTTP ${response.status}`)
    const text = await response.text()
    if (text.length > 262144) throw Error('Response too large')
    return JSON.parse(text)
  } finally {
    clearTimeout(timer)
  }
}
async function loadLocale(code: string): Promise<Record<string, string> | null> {
  if (cache[code]) return cache[code]
  const pending = localeRequests.get(code)
  if (pending) return pending
  const loading = (async () => {
    try {
      const data: unknown = await boundedJson(`/i18n/${code}.json`)
      if (!data || Array.isArray(data) || typeof data !== 'object') return null
      const dict: Record<string, string> = Object.create(null)
      for (const [key, value] of Object.entries(data)) {
        if (!sourceKeys.has(key) || typeof value !== 'string' || !value || value.length > 8192) return null
        dict[key] = value
      }
      if (!Object.keys(dict).length) return null
      cache[code] = dict
      return dict
    } catch {
      return null
    } finally {
      localeRequests.delete(code)
    }
  })()
  localeRequests.set(code, loading)
  return loading
}

// A key missing from the active locale falls back to English.
export const t = (key: Key) => i18n.get().dict[key] ?? source[key]
// Machine states arrive from the backend as English names (see machine_status_name in main.c); state.* keys hold them.
const STATE_KEYS: Record<string, Key> = Object.fromEntries(
  (Object.keys(en) as Key[]).filter(k => k.startsWith('state.')).map(k => [source[k], k])
)
export const tState = (name: string) => (STATE_KEYS[name] ? t(STATE_KEYS[name]) : name)
export const tpl = (key: Key, vars: Record<string, string | number>) => {
  let s = t(key)
  for (const k in vars) s = s.split(`{${k}}`).join(String(vars[k]))
  return s
}

// Saves only what the user changed: saving the theme must not pin the language detected from the browser.
function persist(prefs: { language?: string; theme?: string }) {
  // Persisted preferences are best-effort for the current page, but failed HTTP
  // responses must not silently masquerade as a successful save.
  void request('/api/preferences', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(prefs),
  }).catch((error: unknown) => {
    const message = error instanceof Error ? error.message : String(error)
    notify(tpl('dashboard.save_failed_error', { error: message }), 'error')
  })
}

export async function setLanguage(code: string, save = false) {
  const lang = Object.keys(LANGUAGE_NAMES).includes(code) ? code : 'en'
  const dict = lang === 'en' ? {} : await loadLocale(lang)
  if (!dict) {
    notify(tpl('common.translation_unavailable_lang', { lang }))
    return
  } // keep the current language
  ls.set('cc2-language', lang)
  document.documentElement.lang = lang
  i18n.set({ lang, dict })
  if (save) persist({ language: lang })
}

export function setTheme(mode: string, save = false) {
  const m = themeId(mode)
  ls.set('cc2-theme', m)
  document.documentElement.dataset.theme = m
  theme.set({ mode: m })
  if (save) persist({ theme: m })
}

export async function loadUiPreferences() {
  try {
    const p = await boundedJson('/api/preferences')
    cameraPreferences.set({ persistent: p.camera_persistent === true })
    setTheme(p.theme)
    setQuickFromServer(p.quick_actions)
    await setLanguage(p.language || detectLanguage()) // empty: nothing saved on the printer yet
  } catch {
    setTheme(theme.get().mode)
    await setLanguage(i18n.get().lang)
  } finally {
    // On a first visit there is no previous dictionary to retain after failure.
    if (i18n.get().lang !== 'en' && !Object.keys(i18n.get().dict).length) await setLanguage('en')
  }
}
