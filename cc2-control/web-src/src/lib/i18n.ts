import en from '../../../web/locales/en.json'
import { store, ls } from './store'
import { notify } from './api'
import { setQuickFromServer } from './quick'

export const LANGUAGE_NAMES: Record<string, string> = { en: 'English', it: 'Italiano', fr: 'Français' }

// The browser's first language we ship (fr-CA -> fr), else English.
export const detectLanguage = () =>
  navigator.languages?.map(l => l.slice(0, 2).toLowerCase()).find(l => l in LANGUAGE_NAMES) ?? 'en'

// Every UI string is an identifier from web/locales/en.json (the source language, bundled); other locales load on demand.
export type Key = keyof typeof en
const source: Record<string, string> = en

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
    notify(tpl('common.translation_unavailable_lang', { lang }))
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
    await setLanguage(p.language || detectLanguage()) // empty: nothing saved on the printer yet
  } catch {
    setTheme(theme.get().mode)
    await setLanguage(i18n.get().lang)
  }
}
