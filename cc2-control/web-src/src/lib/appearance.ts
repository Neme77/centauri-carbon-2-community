import { notify, request, errText } from '@/lib/api'
import { t, theme as legacyTheme } from '@/lib/i18n'
import { store } from '@/lib/store'

/* Appearance profiles for the browser dashboard and, where the installed
 * firmware exposes a display manifest, a reversible native-display pack.
 *
 * The server (src/appearance.h) owns persistence and validation. This module
 * keeps the same versioned shape, applies it to CSS custom properties, and
 * offers only bounded, non-executable customisation: named colour tokens,
 * allow-listed widget identifiers, numbers, a font choice and profile names.
 * There is deliberately no free-form CSS or JavaScript field. */

export type WidgetId = 'camera' | 'quick' | 'job' | 'tuning' | 'thermal' | 'fans' | 'firstLayer' | 'position' | 'system'

export type ThemeToken =
  | 'bg'
  | 'panel'
  | 'edge'
  | 'text'
  | 'muted'
  | 'field'
  | 'ink'
  | 'cyan'
  | 'green'
  | 'amber'
  | 'red'
  | 'blue'
  | 'well'
  | 'wellFg'

export const TOKEN_IDS: ThemeToken[] = [
  'bg',
  'panel',
  'edge',
  'text',
  'muted',
  'field',
  'ink',
  'cyan',
  'green',
  'amber',
  'red',
  'blue',
  'well',
  'wellFg',
]

/* CSS custom property for each token. `wellFg` is the one name that differs
 * from the stylesheet (--well-fg). */
export const TOKEN_VARS: Record<ThemeToken, string> = {
  bg: '--bg',
  panel: '--panel',
  edge: '--edge',
  text: '--text',
  muted: '--muted',
  field: '--field',
  ink: '--ink',
  cyan: '--cyan',
  green: '--green',
  amber: '--amber',
  red: '--red',
  blue: '--blue',
  well: '--well',
  wellFg: '--well-fg',
}

export type ThemeTokens = Record<ThemeToken, string>

export interface CustomTheme {
  id: string
  name: string
  tokens: ThemeTokens
}

export interface WidgetPref {
  id: WidgetId
  enabled: boolean
  span: 1 | 2 | 4
  compact: boolean
  accent: string
}

export interface Layout {
  id: string
  name: string
  columns: 2 | 4
  widgets: WidgetPref[]
}

export type Density = 0 | 1 | 2
export type FontChoice = 0 | 1 | 2

export interface DashboardPrefs {
  theme: string
  mode: 'dark' | 'light' | 'auto'
  layout: string
  density: Density
  font: FontChoice
  scale: number
  radius: number
  spacing: 0 | 1 | 2
  animations: boolean
  layouts: Layout[]
}

export interface TouchscreenPrefs {
  enabled: boolean
  linkAccent: boolean
  experimental: boolean
  accent: string
  background: string
  boot: 'community' | 'custom'
}

export interface AppearanceProfile {
  id: string
  name: string
  dashboard: DashboardPrefs
  touchscreen: TouchscreenPrefs
}

export interface AppearanceDoc {
  version: 1
  activeProfile: string
  dashboard: DashboardPrefs
  touchscreen: TouchscreenPrefs
  themes: CustomTheme[]
  profiles: AppearanceProfile[]
  /* Read-only diagnostics from GET /api/appearance; never sent back. */
  recovered?: boolean
  error?: string
}

export const WIDGET_IDS: WidgetId[] = [
  'camera',
  'quick',
  'job',
  'tuning',
  'thermal',
  'fans',
  'firstLayer',
  'position',
  'system',
]

export const WIDGET_TITLE = {
  camera: 'appearance.widget_camera',
  quick: 'dashboard.quick_actions',
  job: 'common.current_job',
  tuning: 'appearance.widget_tuning',
  thermal: 'dashboard.thermals',
  fans: 'common.fans',
  firstLayer: 'dashboard.first_layer',
  position: 'common.position',
  system: 'dashboard.system',
} as const

const DEFAULT_SPAN: Record<WidgetId, 1 | 2 | 4> = {
  camera: 2,
  quick: 2,
  job: 2,
  tuning: 2,
  thermal: 2,
  fans: 1,
  firstLayer: 1,
  position: 2,
  system: 2,
}

export const DEFAULT_WIDGETS = (): WidgetPref[] =>
  WIDGET_IDS.map(id => ({ id, enabled: true, span: DEFAULT_SPAN[id], compact: false, accent: '' }))

export const defaultLayout = (): Layout => ({
  id: 'custom-grid',
  name: 'Custom grid',
  columns: 4,
  widgets: DEFAULT_WIDGETS(),
})

export const classicLayout = (): Layout => ({ id: 'classic', name: 'Classic', columns: 4, widgets: [] })

/* The twelve shipped palettes. Duplicated from index.css only so the theme
 * studio can preview and duplicate them without re-parsing the stylesheet;
 * the runtime still uses the CSS block for a built-in theme. */
export const BUILTIN_TOKENS: Record<string, ThemeTokens> = {
  dark: {
    bg: '#021320',
    panel: '#06202f',
    edge: '#14405a',
    text: '#eef7fc',
    muted: '#94b4c4',
    field: '#0b2c40',
    ink: '#012331',
    cyan: '#00d3ed',
    green: '#29e49b',
    amber: '#ffc54a',
    red: '#ff6170',
    blue: '#3aa8ff',
    well: '#031521',
    wellFg: '#edf6fc',
  },
  light: {
    bg: '#eef1f4',
    panel: '#ffffff',
    edge: '#d3d9de',
    text: '#15191c',
    muted: '#566067',
    field: '#f3f5f7',
    ink: '#ffffff',
    cyan: '#006f80',
    green: '#087748',
    amber: '#815700',
    red: '#ad2638',
    blue: '#166fa8',
    well: '#031521',
    wellFg: '#edf6fc',
  },
  dracula: {
    bg: '#21222c',
    panel: '#282a36',
    edge: '#44475a',
    text: '#f8f8f2',
    muted: '#a4a9c8',
    field: '#343746',
    ink: '#282a36',
    cyan: '#bd93f9',
    green: '#50fa7b',
    amber: '#ffb86c',
    red: '#ff5555',
    blue: '#8be9fd',
    well: '#191a21',
    wellFg: '#f8f8f2',
  },
  nord: {
    bg: '#2e3440',
    panel: '#3b4252',
    edge: '#4c566a',
    text: '#eceff4',
    muted: '#b4bfd3',
    field: '#434c5e',
    ink: '#2e3440',
    cyan: '#9dcddb',
    green: '#a3be8c',
    amber: '#ebcb8b',
    red: '#ffb0b4',
    blue: '#81a1c1',
    well: '#242933',
    wellFg: '#eceff4',
  },
  monokai: {
    bg: '#1e1f1c',
    panel: '#272822',
    edge: '#49483e',
    text: '#f8f8f2',
    muted: '#a6a28c',
    field: '#3e3d32',
    ink: '#272822',
    cyan: '#a6e22e',
    green: '#a6e22e',
    amber: '#fd971f',
    red: '#f92672',
    blue: '#66d9ef',
    well: '#191a15',
    wellFg: '#f8f8f2',
  },
  'solarized-light': {
    bg: '#eee8d5',
    panel: '#fdf6e3',
    edge: '#d6cfb9',
    text: '#073642',
    muted: '#566b72',
    field: '#f5eedb',
    ink: '#fdf6e3',
    cyan: '#156296',
    green: '#5f7000',
    amber: '#856100',
    red: '#b01e1c',
    blue: '#1c78bc',
    well: '#002b36',
    wellFg: '#eee8d5',
  },
  obsidian: {
    bg: '#0b0c12',
    panel: '#15161f',
    edge: '#333345',
    text: '#eeedf7',
    muted: '#a6a5bb',
    field: '#1d1e2a',
    ink: '#100a20',
    cyan: '#b28aff',
    green: '#46dfa4',
    amber: '#ffc66d',
    red: '#ff657f',
    blue: '#48d9ef',
    well: '#090a10',
    wellFg: '#eeedf7',
  },
  aurora: {
    bg: '#dde4f4',
    panel: '#ffffff',
    edge: '#aab8d6',
    text: '#10182f',
    muted: '#44506e',
    field: '#eef2fb',
    ink: '#ffffff',
    cyan: '#4236c9',
    green: '#087246',
    amber: '#7d5200',
    red: '#ac1f38',
    blue: '#0a62b3',
    well: '#0e1530',
    wellFg: '#eaf0ff',
  },
  klipper: {
    bg: '#121212',
    panel: '#1e1e1e',
    edge: '#444444',
    text: '#f2f2f2',
    muted: '#a6a6a6',
    field: '#2a2a2a',
    ink: '#140404',
    cyan: '#ff5a54',
    green: '#5fd068',
    amber: '#ffb42e',
    red: '#ff7d8e',
    blue: '#4aa8ff',
    well: '#0b0b0b',
    wellFg: '#eeeeee',
  },
  dune: {
    bg: '#ebe0cf',
    panel: '#fffdf8',
    edge: '#bfae92',
    text: '#2a2118',
    muted: '#5e5039',
    field: '#f6efe2',
    ink: '#ffffff',
    cyan: '#0a6b6b',
    green: '#2a6b1f',
    amber: '#7a4a00',
    red: '#a8262b',
    blue: '#1f5fa8',
    well: '#2a2118',
    wellFg: '#fbf3e4',
  },
  swamp: {
    bg: '#10130b',
    panel: '#1a1f12',
    edge: '#46513a',
    text: '#eef0df',
    muted: '#aab38f',
    field: '#242b19',
    ink: '#10130b',
    cyan: '#a9b83c',
    green: '#4fd18b',
    amber: '#e6a83c',
    red: '#ff8272',
    blue: '#74b4d6',
    well: '#0a0c06',
    wellFg: '#eef0df',
  },
  klipperscreen: {
    bg: '#1e1e1e',
    panel: '#2b2b2b',
    edge: '#474747',
    text: '#f2f2f2',
    muted: '#b8b8b8',
    field: '#353535',
    ink: '#101820',
    cyan: '#4da3ff',
    green: '#6fd183',
    amber: '#ffb84d',
    red: '#ff7b7b',
    blue: '#4dd0e1',
    well: '#121212',
    wellFg: '#f2f2f2',
  },
}

export const BUILTIN_THEME_IDS = Object.keys(BUILTIN_TOKENS)

export const themeLabel = (id: string): string => {
  const known: Record<string, string> = {
    dark: 'Dark',
    light: 'Light',
    'solarized-light': 'Solarized Light',
    klipperscreen: 'KlipperScreen',
    auto: 'System',
    'custom-grid': '',
  }
  return known[id] ?? id.replace(/(^|-)([a-z])/g, (_, dash, letter) => (dash ? ' ' : '') + letter.toUpperCase())
}

/* Suggested starting points for a custom identity. These are design
 * directions, not copies of third-party branding or assets. */
export const THEME_PRESETS: { id: string; name: string; mode: 'dark' | 'light'; tokens: ThemeTokens }[] = [
  {
    id: 'original',
    name: 'Original ELEGOO',
    mode: 'dark',
    tokens: {
      bg: '#0b1117',
      panel: '#121b22',
      edge: '#24343f',
      text: '#f0f6f8',
      muted: '#9fb3bd',
      field: '#17232c',
      ink: '#061014',
      cyan: '#22c7a9',
      green: '#41d67c',
      amber: '#ffbe3d',
      red: '#ff5f6d',
      blue: '#37a8e8',
      well: '#070c10',
      wellFg: '#eef7fa',
    },
  },
  {
    id: 'community',
    name: 'Community Default',
    mode: 'dark',
    tokens: { ...BUILTIN_TOKENS.dark, cyan: '#00d3ed', green: '#2ee6a6' },
  },
  {
    id: 'klipper-dark',
    name: 'Klipper Dark',
    mode: 'dark',
    tokens: { ...BUILTIN_TOKENS.klipper },
  },
  {
    id: 'klipper-light',
    name: 'Klipper Light',
    mode: 'light',
    tokens: {
      bg: '#f0f0f0',
      panel: '#ffffff',
      edge: '#c6c6c6',
      text: '#171717',
      muted: '#555555',
      field: '#f7f7f7',
      ink: '#ffffff',
      cyan: '#b02a24',
      green: '#256b2a',
      amber: '#7a5200',
      red: '#a51f2b',
      blue: '#145c96',
      well: '#1d1d1d',
      wellFg: '#f0f0f0',
    },
  },
  {
    id: 'minimal',
    name: 'Minimal',
    mode: 'light',
    tokens: {
      bg: '#f7f7f7',
      panel: '#ffffff',
      edge: '#dcdcdc',
      text: '#1b1b1b',
      muted: '#5f5f5f',
      field: '#f2f2f2',
      ink: '#ffffff',
      cyan: '#333333',
      green: '#2f6b3a',
      amber: '#7a5a00',
      red: '#a0313c',
      blue: '#2a5f8f',
      well: '#232323',
      wellFg: '#f4f4f4',
    },
  },
  {
    id: 'industrial',
    name: 'Industrial',
    mode: 'dark',
    tokens: {
      bg: '#16181a',
      panel: '#22262a',
      edge: '#3c434a',
      text: '#eef1f2',
      muted: '#a9b2b8',
      field: '#2b3035',
      ink: '#141414',
      cyan: '#ff8c1a',
      green: '#58c470',
      amber: '#ffb020',
      red: '#ff5d5d',
      blue: '#6fb6ff',
      well: '#0e1012',
      wellFg: '#eef1f2',
    },
  },
  {
    id: 'high-contrast',
    name: 'High Contrast',
    mode: 'dark',
    tokens: {
      bg: '#000000',
      panel: '#000000',
      edge: '#ffffff',
      text: '#ffffff',
      muted: '#ffffff',
      field: '#141400',
      ink: '#000000',
      cyan: '#ffe600',
      green: '#66ff66',
      amber: '#ffcc00',
      red: '#ff6666',
      blue: '#66ccff',
      well: '#000000',
      wellFg: '#ffffff',
    },
  },
  {
    id: 'oled',
    name: 'OLED-inspired',
    mode: 'dark',
    tokens: {
      bg: '#000000',
      panel: '#080808',
      edge: '#242424',
      text: '#f5f5f5',
      muted: '#b0b0b0',
      field: '#111111',
      ink: '#000000',
      cyan: '#35d6ff',
      green: '#4dff88',
      amber: '#ffd24d',
      red: '#ff5a6a',
      blue: '#6aa8ff',
      well: '#000000',
      wellFg: '#f5f5f5',
    },
  },
]

export const DEFAULT_DASHBOARD = (): DashboardPrefs => ({
  theme: 'dark',
  mode: 'dark',
  layout: 'classic',
  density: 0,
  font: 0,
  scale: 1,
  radius: 12,
  spacing: 1,
  animations: true,
  layouts: [classicLayout(), defaultLayout()],
})

export const DEFAULT_TOUCHSCREEN = (): TouchscreenPrefs => ({
  enabled: false,
  linkAccent: true,
  experimental: false,
  accent: '#7eead8',
  background: '#0a0c11',
  boot: 'community',
})

export const DEFAULT_DOC = (): AppearanceDoc => {
  const dashboard = DEFAULT_DASHBOARD()
  const touchscreen = DEFAULT_TOUCHSCREEN()
  return {
    version: 1,
    activeProfile: 'community',
    dashboard,
    touchscreen,
    themes: [],
    profiles: [
      {
        id: 'community',
        name: 'Community Default',
        dashboard: clone(dashboard),
        touchscreen: clone(touchscreen),
      },
    ],
  }
}

export const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T

const docStore = store({ doc: DEFAULT_DOC(), loaded: false, error: '', saving: false, savedAt: 0 })
export const appearance = docStore

const CACHE_KEY = 'cc2-appearance'

/* ---- colour helpers --------------------------------------------------- */

const clamp = (value: number, min = 0, max = 255) => Math.max(min, Math.min(max, value))
export const hexValid = (value: string) => /^#[0-9a-fA-F]{6}$/.test(value)
export const normalizeHex = (value: string) => (hexValid(value) ? value.toLowerCase() : '#000000')

export const hexToRgb = (hex: string): [number, number, number] => {
  const value = normalizeHex(hex).slice(1)
  return [parseInt(value.slice(0, 2), 16), parseInt(value.slice(2, 4), 16), parseInt(value.slice(4, 6), 16)]
}
export const rgbToHex = (r: number, g: number, b: number) =>
  `#${[r, g, b].map(v => clamp(Math.round(v)).toString(16).padStart(2, '0')).join('')}`

export const mix = (a: string, b: string, amount: number) => {
  const [ar, ag, ab] = hexToRgb(a)
  const [br, bg, bb] = hexToRgb(b)
  return rgbToHex(ar + (br - ar) * amount, ag + (bg - ag) * amount, ab + (bb - ab) * amount)
}

export const luminance = (hex: string) => {
  const [r, g, b] = hexToRgb(hex).map(v => {
    const c = v / 255
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4
  })
  return 0.2126 * r + 0.7152 * g + 0.0722 * b
}

export const contrast = (a: string, b: string) => {
  const la = luminance(a)
  const lb = luminance(b)
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
}

export const readableOn = (background: string) =>
  contrast(background, '#ffffff') >= contrast(background, '#000000') ? '#ffffff' : '#000000'

export const isDark = (hex: string) => luminance(hex) < 0.35

export const hsl = (hex: string) => {
  const [r0, g0, b0] = hexToRgb(hex).map(v => v / 255)
  const max = Math.max(r0, g0, b0)
  const min = Math.min(r0, g0, b0)
  const l = (max + min) / 2
  if (max === min) return { h: 0, s: 0, l }
  const d = max - min
  const s = l > 0.5 ? d / (2 - max - min) : d / (max + min)
  let h = 0
  if (max === r0) h = (g0 - b0) / d + (g0 < b0 ? 6 : 0)
  else if (max === g0) h = (b0 - r0) / d + 2
  else h = (r0 - g0) / d + 4
  return { h: h / 6, s, l }
}

export const hslToHex = (h: number, s: number, l: number) => {
  const hue = ((h % 1) + 1) % 1
  if (s === 0) return rgbToHex(l * 255, l * 255, l * 255)
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s
  const p = 2 * l - q
  const channel = (t: number) => {
    let v = t
    if (v < 0) v += 1
    if (v > 1) v -= 1
    if (v < 1 / 6) return p + (q - p) * 6 * v
    if (v < 1 / 2) return q
    if (v < 2 / 3) return p + (q - p) * (2 / 3 - v) * 6
    return p
  }
  return rgbToHex(channel(hue + 1 / 3) * 255, channel(hue) * 255, channel(hue - 1 / 3) * 255)
}

/* Derive a complete, readable palette from a page background and an accent.
 * Text tokens are adjusted until they meet WCAG AA on the page, panel and
 * field; the accent itself is kept as chosen (the caller sees a warning when
 * it is used as text and falls below 4.5:1). */
export const deriveTokens = (background: string, accent: string, mode: 'dark' | 'light'): ThemeTokens => {
  const bg = normalizeHex(background)
  const dark = mode === 'dark'
  const step = dark ? '#ffffff' : '#000000'
  const panel = mix(bg, step, dark ? 0.055 : 0.7)
  const field = mix(bg, step, dark ? 0.095 : 0.5)
  const edge = mix(bg, step, dark ? 0.2 : 0.16)
  let text = dark ? '#f2f6f9' : '#14181c'
  let muted = mix(text, bg, 0.42)
  const ensure = (colour: string, against: string[]) => {
    let out = colour
    for (let i = 0; i < 12 && against.some(b => contrast(out, b) < 4.5); i++)
      out = mix(out, dark ? '#ffffff' : '#000000', 0.12)
    return out
  }
  text = ensure(text, [bg, panel, field])
  muted = ensure(muted, [bg, panel, field])
  return {
    bg,
    panel,
    field,
    edge,
    text,
    muted,
    ink: readableOn(accent),
    cyan: normalizeHex(accent),
    green: dark ? '#3ee08f' : '#087748',
    amber: dark ? '#ffc54a' : '#815700',
    red: dark ? '#ff6170' : '#ad2638',
    blue: dark ? '#3aa8ff' : '#166fa8',
    well: dark ? mix(bg, '#000000', 0.35) : mix(bg, '#001018', 0.85),
    wellFg: dark ? '#edf6fc' : '#edf6fc',
  }
}

/* ---- normalisation ---------------------------------------------------- */

const isObject = (v: unknown): v is Record<string, any> => Boolean(v) && typeof v === 'object' && !Array.isArray(v)
const idOk = (id: unknown) => typeof id === 'string' && /^[a-z0-9][a-z0-9-]{1,39}$/.test(id)
/* A custom theme id is used by the appearance document AND by the legacy
 * /api/preferences theme field: "custom-" plus 2..17 lowercase letters and
 * single interior hyphens, 24 characters in total. src/appearance.h
 * (AP_THEME_ID_MAX) and uc2-control/src/main.c (theme_valid) enforce the same
 * rule; test_theme_contract_static.py compares them. */
export const THEME_ID_MAX = 24
export const themeIdOk = (id: unknown): id is string =>
  typeof id === 'string' && id.length <= THEME_ID_MAX && /^custom-[a-z][a-z-]*[a-z]$/.test(id) && !id.includes('--')
const nameOk = (name: unknown) =>
  typeof name === 'string' &&
  name.trim().length > 0 &&
  name.trim().length <= 64 &&
  ![...name].some(ch => ch.charCodeAt(0) < 32 || ch.charCodeAt(0) === 127)
const bool = (v: unknown, fallback: boolean) => (typeof v === 'boolean' ? v : fallback)
const int = (v: unknown, min: number, max: number, fallback: number) =>
  typeof v === 'number' && Number.isFinite(v) && v >= min && v <= max ? Math.round(v) : fallback

const normalizeWidgets = (raw: unknown): WidgetPref[] => {
  const list = Array.isArray(raw) ? raw : []
  const out: WidgetPref[] = []
  for (const item of list) {
    if (!isObject(item) || !WIDGET_IDS.includes(item.id) || out.some(w => w.id === item.id)) continue
    const span = item.span === 1 || item.span === 4 ? item.span : 2
    out.push({
      id: item.id,
      enabled: bool(item.enabled, true),
      span,
      compact: bool(item.compact, false),
      accent: typeof item.accent === 'string' && hexValid(item.accent) ? item.accent.toLowerCase() : '',
    })
  }
  return out
}

const normalizeLayouts = (raw: unknown): Layout[] => {
  const list = Array.isArray(raw) ? raw : []
  const out: Layout[] = []
  for (const item of list) {
    if (!isObject(item) || !idOk(item.id) || out.some(l => l.id === item.id)) continue
    out.push({
      id: item.id,
      name: nameOk(item.name) ? item.name.trim() : item.id,
      columns: item.columns === 2 ? 2 : 4,
      widgets: normalizeWidgets(item.widgets),
    })
    if (out.length >= 4) break
  }
  if (!out.some(l => l.id === 'classic')) out.unshift(classicLayout())
  if (!out.some(l => l.id === 'custom-grid')) out.push(defaultLayout())
  return out.slice(0, 4)
}

const normalizeDashboard = (raw: unknown, fallback: DashboardPrefs): DashboardPrefs => {
  const source = isObject(raw) ? raw : {}
  const layouts = normalizeLayouts(source.layouts)
  const layout =
    typeof source.layout === 'string' && layouts.some(l => l.id === source.layout) ? source.layout : 'classic'
  const theme = typeof source.theme === 'string' && source.theme.length <= 40 ? source.theme : fallback.theme
  const mode = source.mode === 'light' || source.mode === 'auto' ? source.mode : 'dark'
  return {
    theme,
    mode,
    layout,
    density: int(source.density, 0, 2, fallback.density) as Density,
    font: int(source.font, 0, 2, fallback.font) as FontChoice,
    scale:
      typeof source.scale === 'number' && source.scale >= 0.85 && source.scale <= 1.3 ? source.scale : fallback.scale,
    radius: int(source.radius, 0, 24, fallback.radius),
    spacing: int(source.spacing, 0, 2, fallback.spacing) as 0 | 1 | 2,
    animations: bool(source.animations, fallback.animations),
    layouts,
  }
}

const normalizeTokens = (raw: unknown): ThemeTokens | null => {
  if (!isObject(raw)) return null
  const tokens = {} as ThemeTokens
  for (const token of TOKEN_IDS) {
    if (typeof raw[token] !== 'string' || !hexValid(raw[token])) return null
    tokens[token] = raw[token].toLowerCase()
  }
  return tokens
}

export const normalizeThemes = (raw: unknown): CustomTheme[] => {
  const list = Array.isArray(raw) ? raw : []
  const out: CustomTheme[] = []
  for (const item of list) {
    if (!isObject(item) || typeof item.id !== 'string') continue
    const id = item.id.startsWith('custom-') ? item.id : `custom-${item.id}`
    if (!themeIdOk(id)) continue
    const tokens = normalizeTokens(item.tokens)
    if (!tokens || out.some(t => t.id === id)) continue
    out.push({ id, name: nameOk(item.name) ? item.name.trim() : id, tokens })
    if (out.length >= 8) break
  }
  return out
}

const normalizeTouchscreen = (raw: unknown, fallback: TouchscreenPrefs): TouchscreenPrefs => {
  const source = isObject(raw) ? raw : {}
  return {
    enabled: bool(source.enabled, fallback.enabled),
    linkAccent: bool(source.linkAccent, fallback.linkAccent),
    experimental: bool(source.experimental, fallback.experimental),
    accent:
      typeof source.accent === 'string' && hexValid(source.accent) ? source.accent.toLowerCase() : fallback.accent,
    background:
      typeof source.background === 'string' && hexValid(source.background)
        ? source.background.toLowerCase()
        : fallback.background,
    boot: source.boot === 'custom' ? 'custom' : 'community',
  }
}

const normalizeProfile = (raw: unknown, fallback?: AppearanceProfile): AppearanceProfile | null => {
  if (!isObject(raw) || !idOk(raw.id)) return null
  return {
    id: raw.id,
    name: nameOk(raw.name) ? raw.name.trim() : (fallback?.name ?? raw.id),
    dashboard: normalizeDashboard(raw.dashboard, fallback?.dashboard ?? DEFAULT_DASHBOARD()),
    touchscreen: normalizeTouchscreen(raw.touchscreen, fallback?.touchscreen ?? DEFAULT_TOUCHSCREEN()),
  }
}

export const normalizeDoc = (raw: unknown): AppearanceDoc => {
  if (!isObject(raw)) return DEFAULT_DOC()
  const base = DEFAULT_DOC()
  const dashboard = normalizeDashboard(raw.dashboard, base.dashboard)
  const touchscreen = normalizeTouchscreen(raw.touchscreen, base.touchscreen)
  const themes = normalizeThemes(raw.themes)
  const profiles: AppearanceProfile[] = []
  for (const item of Array.isArray(raw.profiles) ? raw.profiles : []) {
    const profile = normalizeProfile(item)
    if (profile && !profiles.some(p => p.id === profile.id)) profiles.push(profile)
    if (profiles.length >= 6) break
  }
  if (!profiles.some(p => p.id === 'community')) profiles.unshift(base.profiles[0])
  const activeProfile = idOk(raw.activeProfile) ? raw.activeProfile : 'community'
  return {
    version: 1,
    activeProfile,
    dashboard,
    touchscreen,
    themes,
    profiles,
    recovered: raw.recovered === true,
    error: typeof raw.error === 'string' ? raw.error : '',
  }
}

/* ---- apply to the document -------------------------------------------- */

let autoQuery: MediaQueryList | null = null
const applyAutoListener = () => {
  if (!autoQuery) {
    autoQuery = matchMedia('(prefers-color-scheme: light)')
    autoQuery.addEventListener?.('change', () => {
      if (docStore.get().doc.dashboard.theme === 'auto') applyAppearance()
    })
  }
}

export const effectiveThemeId = (doc: AppearanceDoc = docStore.get().doc): string => {
  const id = doc.dashboard.theme
  if (id === 'auto') return matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark'
  return id
}

export const effectiveTokens = (doc: AppearanceDoc = docStore.get().doc): ThemeTokens => {
  const custom = doc.themes.find(theme => theme.id === doc.dashboard.theme)
  if (custom) return custom.tokens
  return BUILTIN_TOKENS[effectiveThemeId(doc)] ?? BUILTIN_TOKENS.dark
}

export const applyAppearance = (doc: AppearanceDoc = docStore.get().doc) => {
  if (typeof document === 'undefined') return
  const root = document.documentElement
  const selected = doc.dashboard.theme
  const custom = doc.themes.find(theme => theme.id === selected)
  const resolved = custom ? 'custom' : effectiveThemeId(doc)
  root.dataset.theme = resolved
  for (const token of TOKEN_IDS) root.style.removeProperty(TOKEN_VARS[token])
  if (custom) {
    for (const token of TOKEN_IDS) root.style.setProperty(TOKEN_VARS[token], custom.tokens[token])
    root.style.colorScheme = isDark(custom.tokens.bg) ? 'dark' : 'light'
  } else {
    root.style.colorScheme = resolved === 'light' ? 'light' : 'dark'
  }
  /* The shipped palettes define their own corner language (Klipper 6 px,
   * KlipperScreen 18 px). Only a custom palette or an explicit non-default
   * radius overrides it. */
  if (custom || doc.dashboard.radius !== 12) root.style.setProperty('--radius-card', `${doc.dashboard.radius}px`)
  else root.style.removeProperty('--radius-card')
  root.style.setProperty('--cc2-font-scale', String(doc.dashboard.scale))
  root.style.setProperty('--cc2-space', String([0.82, 1, 1.18][doc.dashboard.spacing]))
  root.dataset.density = ['comfortable', 'compact', 'spacious'][doc.dashboard.density]
  root.dataset.font = ['inter', 'system', 'mono'][doc.dashboard.font]
  root.dataset.motion = doc.dashboard.animations ? 'on' : 'off'
  if (selected === 'auto') applyAutoListener()
  try {
    localStorage.setItem('cc2-theme', selected)
  } catch {
    /* private mode */
  }
  legacyTheme.set({ mode: selected })
}

/* ---- persistence ------------------------------------------------------ */

const cache = (doc: AppearanceDoc) => {
  try {
    localStorage.setItem(CACHE_KEY, JSON.stringify(doc))
  } catch {
    /* private mode */
  }
}

export const cachedAppearance = (): AppearanceDoc | null => {
  try {
    const raw = localStorage.getItem(CACHE_KEY)
    return raw ? normalizeDoc(JSON.parse(raw)) : null
  } catch {
    return null
  }
}

export async function loadAppearance() {
  const cached = cachedAppearance()
  if (cached) {
    docStore.set({ doc: cached })
    applyAppearance(cached)
  }
  try {
    const raw = await request('/api/appearance')
    const doc = normalizeDoc(raw)
    docStore.set({ doc, loaded: true, error: '' })
    cache(doc)
    applyAppearance(doc)
  } catch (error) {
    docStore.set({ loaded: true, error: errText(error) })
    if (!cached) applyAppearance(docStore.get().doc)
  }
}

let saveTimer = 0
let saveInFlight = false
let saveDirty = false

/* The document PUT never carries the theme library: adding or changing a theme
 * is an explicit per-item request, so two browsers cannot delete each other's
 * themes by saving a stale array. The server still merges a "themes" array for
 * older clients and imports. */
const payload = (doc: AppearanceDoc) => ({
  activeProfile: doc.activeProfile,
  dashboard: doc.dashboard,
  touchscreen: doc.touchscreen,
})

const saveThemeRequest = async (theme: CustomTheme) => {
  try {
    await request('/api/appearance/themes', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ theme }),
    })
    return true
  } catch (error) {
    notify(errText(error), 'error')
    await loadAppearance()
    return false
  }
}

export async function saveAppearance(): Promise<void> {
  if (saveInFlight) {
    saveDirty = true
    return
  }
  saveInFlight = true
  const doc = docStore.get().doc
  docStore.set({ saving: true })
  try {
    await request('/api/appearance', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload(doc)),
    })
    docStore.set({ saving: false, savedAt: Date.now(), error: '' })
  } catch (error) {
    docStore.set({ saving: false, error: errText(error) })
    throw error
  } finally {
    saveInFlight = false
    if (saveDirty) {
      saveDirty = false
      void saveAppearance().catch(error => notify(errText(error), 'error'))
    }
  }
}

export const scheduleSave = () => {
  clearTimeout(saveTimer)
  saveTimer = window.setTimeout(() => {
    void saveAppearance().catch(error => notify(errText(error), 'error'))
  }, 350)
}

export const updateDoc = (change: (doc: AppearanceDoc) => void, save = true) => {
  const next = clone(docStore.get().doc)
  change(next)
  /* A linked touchscreen follows the dashboard accent automatically; the
   * touchscreen tab shows the field read-only while the link is on. */
  if (next.touchscreen.linkAccent) next.touchscreen.accent = effectiveTokens(next).cyan
  docStore.set({ doc: next })
  cache(next)
  applyAppearance(next)
  if (save) scheduleSave()
}

/* ---- themes ----------------------------------------------------------- */

/* Theme identifiers also travel through the legacy /api/preferences field,
 * whose contract is lowercase letters joined by single hyphens. Keep the
 * generated slug inside that shape (never digits) so both stores accept it. */
export const slug = (name: string) =>
  name
    .toLowerCase()
    .normalize('NFKD')
    .replace(/[^a-z]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 17) || 'theme'

export const uniqueThemeId = (doc: AppearanceDoc, name: string) => {
  const limit = THEME_ID_MAX
  const base = `custom-${slug(name)}`.slice(0, limit).replace(/-+$/g, '')
  const taken = (candidate: string) =>
    doc.themes.some(theme => theme.id === candidate) || BUILTIN_THEME_IDS.includes(candidate)
  if (!taken(base)) return base
  for (let suffix = 0; suffix < 26; suffix++) {
    const tail = `-${String.fromCharCode(97 + suffix)}`
    const candidate = base.slice(0, limit - tail.length).replace(/-+$/g, '') + tail
    if (!taken(candidate)) return candidate
  }
  return `${base.slice(0, limit - 2).replace(/-+$/g, '')}-z`
}

export const createTheme = (name: string, tokens: ThemeTokens, applyNow = true): CustomTheme | null => {
  const current = docStore.get().doc
  const validTokens = normalizeTokens(tokens)
  if (!validTokens) {
    notify(t('appearance.error_theme_invalid'), 'error')
    return null
  }
  if (current.themes.length >= 8) {
    notify(t('appearance.error_theme_limit'), 'error')
    return null
  }
  const trimmed = typeof name === 'string' ? name.trim().slice(0, 64) : ''
  const theme: CustomTheme = {
    id: uniqueThemeId(current, trimmed || 'Custom'),
    name: trimmed || 'Custom',
    tokens: validTokens,
  }
  updateDoc(doc => {
    doc.themes.push(theme)
    if (applyNow) {
      doc.dashboard.theme = theme.id
      doc.dashboard.mode = isDark(theme.tokens.bg) ? 'dark' : 'light'
    }
  }, false)
  /* The theme must exist on the printer before a dashboard that names it is
   * saved, otherwise the server's normaliser would fall back to the default. */
  void (async () => {
    if ((await saveThemeRequest(theme)) && applyNow) {
      await saveAppearance().catch(error => notify(errText(error), 'error'))
    }
  })()
  return theme
}

export const updateTheme = (theme: CustomTheme) => {
  const validTokens = normalizeTokens(theme.tokens)
  if (!validTokens || !themeIdOk(theme.id)) {
    notify(t('appearance.error_theme_invalid'), 'error')
    return
  }
  const next = { ...theme, tokens: validTokens }
  updateDoc(doc => {
    const index = doc.themes.findIndex(item => item.id === next.id)
    if (index >= 0) doc.themes[index] = { ...next }
  }, false)
  void saveThemeRequest(next)
}

export const deleteTheme = async (id: string) => {
  const wasActive = docStore.get().doc.dashboard.theme === id
  updateDoc(doc => {
    doc.themes = doc.themes.filter(theme => theme.id !== id)
    if (doc.dashboard.theme === id) doc.dashboard.theme = 'dark'
  }, false)
  try {
    await request('/api/appearance/themes/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id }),
    })
    if (wasActive) await saveAppearance().catch(error => notify(errText(error), 'error'))
  } catch (error) {
    notify(errText(error), 'error')
    await loadAppearance()
  }
}

export const selectTheme = (id: string) => {
  // An explicit palette choice is saved at once: a reload immediately after
  // the click must not overtake the debounced colour-picker writes.
  updateDoc(doc => {
    doc.dashboard.theme = id
    if (id === 'auto') doc.dashboard.mode = 'auto'
    else if (id.startsWith('custom-')) {
      const theme = doc.themes.find(item => item.id === id)
      if (theme) doc.dashboard.mode = isDark(theme.tokens.bg) ? 'dark' : 'light'
    } else
      doc.dashboard.mode =
        id === 'light' || id === 'solarized-light' || id === 'aurora' || id === 'dune' ? 'light' : 'dark'
  }, false)
  void saveAppearance().catch(error => notify(errText(error), 'error'))
}

/* ---- layouts and widgets ---------------------------------------------- */

export const activeLayout = (doc: AppearanceDoc = docStore.get().doc): Layout | null =>
  doc.dashboard.layout === 'classic' ? null : dictLayout(doc)

const dictLayout = (doc: AppearanceDoc) =>
  doc.dashboard.layouts.find(layout => layout.id === doc.dashboard.layout) ?? doc.dashboard.layouts[1] ?? null

export const updateLayout = (change: (layout: Layout) => void) => {
  updateDoc(doc => {
    if (doc.dashboard.layout === 'classic') return
    const layout = doc.dashboard.layouts.find(item => item.id === doc.dashboard.layout)
    if (layout) change(layout)
  })
}

export const moveWidget = (id: WidgetId, delta: number) => {
  updateLayout(layout => {
    const index = layout.widgets.findIndex(widget => widget.id === id)
    const target = index + delta
    if (index < 0 || target < 0 || target >= layout.widgets.length) return
    const [item] = layout.widgets.splice(index, 1)
    layout.widgets.splice(target, 0, item)
  })
}

export const dropWidget = (from: WidgetId, to: WidgetId) => {
  if (from === to) return
  updateLayout(layout => {
    const index = layout.widgets.findIndex(widget => widget.id === from)
    const target = layout.widgets.findIndex(widget => widget.id === to)
    if (index < 0 || target < 0) return
    const [item] = layout.widgets.splice(index, 1)
    layout.widgets.splice(target, 0, item)
  })
}

export const newLayout = (name: string) => {
  updateDoc(doc => {
    if (doc.dashboard.layouts.length >= 4) return
    const id = `grid-${Date.now().toString(36)}`
    doc.dashboard.layouts.push({
      id,
      name: name.trim().slice(0, 64) || 'Layout',
      columns: 4,
      widgets: DEFAULT_WIDGETS(),
    })
    doc.dashboard.layout = id
  })
}

export const duplicateLayout = (id: string) => {
  updateDoc(doc => {
    if (doc.dashboard.layouts.length >= 4) return
    const source = doc.dashboard.layouts.find(layout => layout.id === id)
    if (!source) return
    const next = clone(source)
    next.id = `grid-${Date.now().toString(36)}`
    next.name = `${source.name} copy`
    doc.dashboard.layouts.push(next)
    doc.dashboard.layout = next.id
  })
}

export const deleteLayout = (id: string) => {
  updateDoc(doc => {
    if (id === 'classic' || id === 'custom-grid' || doc.dashboard.layouts.length <= 2) return
    doc.dashboard.layouts = doc.dashboard.layouts.filter(layout => layout.id !== id)
    if (doc.dashboard.layout === id) doc.dashboard.layout = 'classic'
  })
}

/* ---- profiles --------------------------------------------------------- */

export const applyProfile = async (id: string) => {
  try {
    await request('/api/appearance/profiles/apply', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id }),
    })
    await loadAppearance()
  } catch (error) {
    notify(errText(error), 'error')
  }
}

export const saveProfile = async (name: string, id?: string) => {
  const doc = docStore.get().doc
  let profileId = id ?? `profile-${slug(name)}`
  if (!id) {
    let suffix = 0
    while (doc.profiles.some(profile => profile.id === profileId)) {
      suffix = (suffix + 1) % 26
      profileId = `profile-${slug(name)}-${String.fromCharCode(97 + suffix)}`
    }
  }
  const existing = id ? doc.profiles.find(profile => profile.id === id) : undefined
  const profile: AppearanceProfile = {
    id: profileId,
    name: name.trim().slice(0, 64) || profileId,
    // Renaming keeps the profile's own snapshot; a new profile captures the
    // active appearance.
    dashboard: clone(existing?.dashboard ?? doc.dashboard),
    touchscreen: clone(existing?.touchscreen ?? doc.touchscreen),
  }
  const activeTheme = doc.themes.find(theme => theme.id === profile.dashboard.theme)
  try {
    await request('/api/appearance/profiles', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(activeTheme ? { profile, theme: activeTheme } : { profile }),
    })
    await loadAppearance()
    return true
  } catch (error) {
    notify(errText(error), 'error')
    return false
  }
}

export const duplicateProfile = async (id: string) => {
  const doc = docStore.get().doc
  const source = doc.profiles.find(profile => profile.id === id)
  if (!source) return false
  const name = `${source.name} ${t('appearance.copy_suffix')}`.trim()
  let nextId = `profile-${slug(name)}`
  let suffix = 0
  while (doc.profiles.some(profile => profile.id === nextId)) {
    suffix = (suffix + 1) % 26
    nextId = `profile-${slug(name)}-${String.fromCharCode(97 + suffix)}`
  }
  const profile: AppearanceProfile = {
    id: nextId,
    name: name.slice(0, 64),
    dashboard: clone(source.dashboard),
    touchscreen: clone(source.touchscreen),
  }
  const activeTheme = doc.themes.find(theme => theme.id === profile.dashboard.theme)
  try {
    await request('/api/appearance/profiles', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(activeTheme ? { profile, theme: activeTheme } : { profile }),
    })
    await loadAppearance()
    return true
  } catch (error) {
    notify(errText(error), 'error')
    return false
  }
}

export const removeProfile = async (id: string) => {
  try {
    await request('/api/appearance/profiles/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id }),
    })
    await loadAppearance()
    return true
  } catch (error) {
    notify(errText(error), 'error')
    return false
  }
}

export const resetAppearance = async (profiles = false) => {
  try {
    await request('/api/appearance/reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ profiles }),
    })
    await loadAppearance()
    return true
  } catch (error) {
    notify(errText(error), 'error')
    return false
  }
}

/* ---- import / export -------------------------------------------------- */

export const exportAppearance = () => ({
  format: 'cc2-appearance',
  version: 1,
  exportedAt: new Date().toISOString(),
  activeProfile: docStore.get().doc.activeProfile,
  dashboard: docStore.get().doc.dashboard,
  touchscreen: docStore.get().doc.touchscreen,
  themes: docStore.get().doc.themes,
  profiles: docStore.get().doc.profiles,
})

export const importAppearance = async (raw: unknown) => {
  if (!isObject(raw)) {
    notify(t('appearance.error_not_export'), 'error')
    return false
  }
  if (typeof raw.version === 'number' && raw.version > 1) {
    notify(t('appearance.error_newer_export'), 'error')
    return false
  }
  // A profile export can be imported through the same button.
  if (raw.format === 'cc2-profile') {
    const profile = normalizeProfile(raw.profile)
    if (!profile) {
      notify(t('appearance.error_not_export'), 'error')
      return false
    }
    const theme = normalizeThemes(raw.theme ? [raw.theme] : [])[0]
    try {
      await request('/api/appearance/profiles', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(theme ? { profile, theme } : { profile }),
      })
      await loadAppearance()
      return true
    } catch (error) {
      notify(errText(error), 'error')
      return false
    }
  }
  if (raw.format !== undefined && raw.format !== 'cc2-appearance') {
    notify(t('appearance.error_not_export'), 'error')
    return false
  }
  const doc = normalizeDoc(raw)
  /* Refuse before writing anything when the printer cannot hold the imported
   * profiles or themes, instead of leaving a half-applied import behind. */
  const current = docStore.get().doc
  const extraProfiles = doc.profiles.filter(
    profile => profile.id !== 'community' && !current.profiles.some(item => item.id === profile.id)
  ).length
  if (current.profiles.length + extraProfiles > 6) {
    notify(t('appearance.profile_limit'), 'error')
    return false
  }
  const extraThemes = doc.themes.filter(theme => !current.themes.some(item => item.id === theme.id)).length
  if (current.themes.length + extraThemes > 8) {
    notify(t('appearance.error_theme_limit'), 'error')
    return false
  }
  try {
    /* Themes first: the dashboard save resolves a custom theme id against the
     * stored library, so an import must not name a theme that does not exist
     * yet. Per-item writes also keep libraries that other browsers added. */
    for (const theme of doc.themes) {
      await request('/api/appearance/themes', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ theme }),
      })
    }
    await request('/api/appearance', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload(doc)),
    })
    for (const profile of doc.profiles.slice(0, 6)) {
      if (profile.id === 'community') continue
      const theme = doc.themes.find(item => item.id === profile.dashboard.theme)
      await request('/api/appearance/profiles', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(theme ? { profile, theme } : { profile }),
      })
    }
    await loadAppearance()
    return true
  } catch (error) {
    notify(errText(error), 'error')
    return false
  }
}

export const downloadJson = (name: string, value: unknown) => {
  const text = `${JSON.stringify(value, null, 2)}\n`
  if (text.length > 256 * 1024) {
    notify(t('appearance.error_export_too_large'), 'error')
    return
  }
  const blob = new Blob([text], { type: 'application/json;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = name
  document.body.appendChild(link)
  link.click()
  link.remove()
  window.setTimeout(() => URL.revokeObjectURL(url), 1000)
}
