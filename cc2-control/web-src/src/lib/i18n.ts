import { store, ls } from './store'
import { notify } from './api'
import { setQuickFromServer } from './quick'

export const LANGUAGE_NAMES: Record<string, string> = { en: 'English', it: 'Italiano', fr: 'Français' }

export const i18n = store({ lang: ls.get('cc2-language') || 'en', dict: {} as Record<string, string> })
// Palettes are defined in index.css; the backend only stores the identifier (see preferences_theme in main.c).
export const THEMES = [
  { id: 'dark', label: 'Dark' },
  { id: 'light', label: 'Light' },
  { id: 'dracula', label: 'Dracula' },
  { id: 'nord', label: 'Nord' },
  { id: 'monokai', label: 'Monokai' },
  { id: 'solarized-light', label: 'Solarized Light' },
]
const themeId = (mode: unknown) => (THEMES.some(x => x.id === mode) ? (mode as string) : 'dark')
export const theme = store({ mode: themeId(ls.get('cc2-theme')) })

const cache: Record<string, Record<string, string>> = {}
async function loadLocale(code: string) {
  if (cache[code]) return cache[code]
  try {
    const r = await fetch(`/i18n/${code}.json`, { cache: 'no-store' })
    if (r.ok) {
      cache[code] = await r.json()
      return cache[code]
    }
  } catch {
    /* handled below */
  }
  return null
}

// English text is the key; a missing key falls back to the English text itself.
export const t = (text: string) => i18n.get().dict[text] ?? text
export const tpl = (key: string, vars: Record<string, string | number>) => {
  let s = t(key)
  for (const k in vars) s = s.split(`{${k}}`).join(String(vars[k]))
  return s
}

function persist() {
  void fetch('/api/preferences', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ language: i18n.get().lang, theme: theme.get().mode }),
  }).catch(() => {})
}

export async function setLanguage(code: string, save = false) {
  const lang = LANGUAGE_NAMES[code] ? code : 'en'
  const dict = lang === 'en' ? {} : await loadLocale(lang)
  if (!dict) {
    notify(tpl('Translation unavailable: {lang}', { lang }))
    return
  } // keep the current language
  ls.set('cc2-language', lang)
  document.documentElement.lang = lang
  i18n.set({ lang, dict })
  if (save) persist()
}

export function setTheme(mode: string, save = false) {
  const m = themeId(mode)
  ls.set('cc2-theme', m)
  document.documentElement.dataset.theme = m
  theme.set({ mode: m })
  if (save) persist()
}

export async function loadUiPreferences() {
  try {
    const p = await (await fetch('/api/preferences', { cache: 'no-store' })).json()
    setTheme(p.theme)
    setQuickFromServer(p.quick_actions)
    await setLanguage(p.language)
  } catch {
    setTheme(theme.get().mode)
    await setLanguage(i18n.get().lang)
  }
}
