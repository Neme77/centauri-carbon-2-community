import type { ComponentChildren } from 'preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import {
  ArrowDown,
  ArrowUp,
  Check,
  ChevronRight,
  Copy,
  Download,
  GripVertical,
  LayoutGrid,
  Palette,
  Plus,
  RotateCcw,
  Save,
  SlidersHorizontal,
  Smartphone,
  Sparkles,
  Trash2,
  Upload,
  UserRound,
  Wand2,
} from 'lucide-preact'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { Button } from '@/components/ui/button'
import { Input, Select, Switch } from '@/components/ui/field'
import { Tabs } from '@/components/ui/tabs'
import { Dot, Tag } from '@/components/ui/badge'
import { ask } from '@/lib/confirm'
import { errText, notify } from '@/lib/api'
import { type Key, t, tpl, THEME_CHOICES } from '@/lib/i18n'
import { cn } from '@/lib/utils'
import { usePoll } from '@/lib/poll'
import { openAppearance, type AppearanceTab, nav } from '@/lib/state'
import {
  appearance,
  applyProfile,
  applyAppearance,
  BUILTIN_THEME_IDS,
  BUILTIN_TOKENS,
  contrast,
  createTheme,
  DEFAULT_DASHBOARD,
  deleteLayout,
  deleteTheme,
  deriveTokens,
  downloadJson,
  dropWidget,
  duplicateLayout,
  duplicateProfile,
  effectiveTokens,
  exportAppearance,
  hexValid,
  importAppearance,
  isDark,
  loadAppearance,
  moveWidget,
  newLayout,
  normalizeThemes,
  removeProfile,
  resetAppearance,
  saveAppearance,
  saveProfile,
  selectTheme,
  THEME_PRESETS,
  themeLabel,
  type CustomTheme,
  type Layout,
  type ThemeToken,
  type ThemeTokens,
  updateDoc,
  updateLayout,
  updateTheme,
  WIDGET_TITLE,
  type WidgetId,
} from '@/lib/appearance'
import { applyPack, buildPack, display, loadDisplay, reasonKey, restoreDisplay } from '@/lib/display'

const TABS: [typeof Palette, Key, AppearanceTab][] = [
  [Palette, 'appearance.overview', 'overview'],
  [Sparkles, 'appearance.themes', 'themes'],
  [LayoutGrid, 'appearance.layout', 'layout'],
  [SlidersHorizontal, 'appearance.widgets', 'widgets'],
  [Smartphone, 'appearance.touchscreen', 'touchscreen'],
  [UserRound, 'appearance.profiles', 'profiles'],
  [Download, 'appearance.transfer', 'transfer'],
  [Wand2, 'appearance.advanced', 'advanced'],
]

const FieldRow = ({
  label,
  hint,
  children,
  htmlFor,
}: {
  label: Key
  hint?: Key
  children: ComponentChildren
  htmlFor?: string
}) => (
  <div class="my-2.5 grid items-center gap-x-3.5 gap-y-1 cc2-md:grid-cols-[170px_minmax(0,1fr)]">
    {htmlFor ? (
      <label htmlFor={htmlFor} class="font-semibold">
        {t(label)}
      </label>
    ) : (
      <span class="font-semibold">{t(label)}</span>
    )}
    <div class="min-w-0">{children}</div>
    {hint && <span class="text-xs text-muted cc2-md:col-start-2">{t(hint)}</span>}
  </div>
)

const ColorField = ({
  id,
  label,
  value,
  onChange,
  disabled,
}: {
  id: string
  label: Key
  value: string
  onChange: (value: string) => void
  disabled?: boolean
}) => {
  const [text, setText] = useState(value)
  useEffect(() => setText(value), [value])
  const commit = (next: string) => {
    if (hexValid(next)) onChange(next.toLowerCase())
    else setText(value)
  }
  return (
    <div class="flex min-w-0 items-center gap-2">
      <input
        id={id}
        type="color"
        aria-label={t(label)}
        value={value}
        disabled={disabled}
        onInput={e => {
          setText(e.currentTarget.value)
          onChange(e.currentTarget.value.toLowerCase())
        }}
        class="size-9 shrink-0 cursor-pointer rounded-md border border-edge bg-field p-1 disabled:opacity-50"
      />
      <input
        aria-label={`${t(label)} — ${t('appearance.color_hex')}`}
        value={text}
        disabled={disabled}
        spellcheck={false}
        onInput={e => setText(e.currentTarget.value)}
        onBlur={() => commit(text)}
        onKeyDown={e => e.key === 'Enter' && commit(text)}
        class="min-h-9 w-28 rounded-md border border-edge bg-field px-2 font-mono text-xs text-fg disabled:opacity-50"
      />
    </div>
  )
}

const ColorGroups: { title: Key; tokens: ThemeToken[] }[] = [
  { title: 'appearance.group_surfaces', tokens: ['bg', 'panel', 'field', 'well'] },
  { title: 'appearance.group_text', tokens: ['text', 'muted', 'ink', 'wellFg'] },
  { title: 'appearance.group_accents', tokens: ['cyan', 'green', 'amber', 'red', 'blue'] },
  { title: 'appearance.group_lines', tokens: ['edge'] },
]

const TokenLabels: Record<ThemeToken, Key> = {
  bg: 'appearance.token_bg',
  panel: 'appearance.token_panel',
  field: 'appearance.token_field',
  well: 'appearance.token_well',
  text: 'appearance.token_text',
  muted: 'appearance.token_muted',
  ink: 'appearance.token_ink',
  wellFg: 'appearance.token_well_fg',
  cyan: 'appearance.token_accent',
  green: 'appearance.token_green',
  amber: 'appearance.token_amber',
  red: 'appearance.token_red',
  blue: 'appearance.token_blue',
  edge: 'appearance.token_edge',
}

const MiniPreview = ({ tokens }: { tokens: ThemeTokens }) => (
  <div class="rounded-lg border p-2.5" style={{ background: tokens.bg, color: tokens.text, borderColor: tokens.edge }}>
    <div class="mb-2 flex items-center gap-2">
      <span class="size-2 rounded-full" style={{ background: tokens.cyan }} />
      <strong class="text-sm">CC2 Control</strong>
      <span class="ml-auto rounded px-1.5 py-0.5 text-xs" style={{ background: tokens.cyan, color: tokens.ink }}>
        {t('appearance.preview_action')}
      </span>
    </div>
    <div class="grid grid-cols-2 gap-2">
      <div class="rounded-md border p-2" style={{ background: tokens.panel, borderColor: tokens.edge }}>
        <div class="text-xs" style={{ color: tokens.muted }}>
          {t('common.nozzle')}
        </div>
        <div class="text-lg font-semibold">210 °C</div>
        <div class="mt-1 h-1 rounded" style={{ background: tokens.edge }}>
          <div class="h-1 w-2/3 rounded" style={{ background: tokens.cyan }} />
        </div>
      </div>
      <div class="rounded-md border p-2" style={{ background: tokens.field, borderColor: tokens.edge }}>
        <div class="text-xs" style={{ color: tokens.muted }}>
          {t('common.heated_bed')}
        </div>
        <div class="text-lg font-semibold">60 °C</div>
        <div class="mt-1 flex gap-1">
          <span class="size-2 rounded-full" style={{ background: tokens.green }} />
          <span class="size-2 rounded-full" style={{ background: tokens.amber }} />
          <span class="size-2 rounded-full" style={{ background: tokens.red }} />
        </div>
      </div>
    </div>
  </div>
)

const contrastChecks = (tokens: ThemeTokens) =>
  [
    { id: 'text_bg', a: 'text', b: 'bg', value: contrast(tokens.text, tokens.bg) },
    { id: 'text_panel', a: 'text', b: 'panel', value: contrast(tokens.text, tokens.panel) },
    { id: 'muted_field', a: 'muted', b: 'field', value: contrast(tokens.muted, tokens.field) },
    { id: 'accent_panel', a: 'cyan', b: 'panel', value: contrast(tokens.cyan, tokens.panel) },
    { id: 'ink_accent', a: 'ink', b: 'cyan', value: contrast(tokens.ink, tokens.cyan) },
  ] as const

const ContrastList = ({ tokens }: { tokens: ThemeTokens }) => (
  <ul class="mt-2 grid gap-1 text-xs">
    {contrastChecks(tokens).map(check => (
      <li key={check.id} class="flex items-center gap-2">
        <Dot c={check.value >= 4.5 ? 'green' : check.value >= 3 ? 'amber' : 'red'} />
        <span class="text-muted">
          {t(TokenLabels[check.a])} / {t(TokenLabels[check.b])}
        </span>
        <b class="ml-auto tabular-nums">{check.value.toFixed(1)}:1</b>
      </li>
    ))}
  </ul>
)

const ThemeEditor = ({ theme, onClose }: { theme: CustomTheme | null; onClose: () => void }) => {
  const source = theme ?? {
    id: '',
    name: t('appearance.new_theme_name'),
    tokens: { ...effectiveTokens() },
  }
  const [name, setName] = useState(source.name)
  const [tokens, setTokens] = useState<ThemeTokens>({ ...source.tokens })
  const [preset, setPreset] = useState('')
  const editing = Boolean(theme)
  const set = (token: ThemeToken, value: string) => setTokens(current => ({ ...current, [token]: value }))
  const applyPreset = (id: string) => {
    setPreset(id)
    const found = THEME_PRESETS.find(item => item.id === id)
    if (!found) return
    setTokens({ ...found.tokens })
    if (!editing) setName(found.name)
  }
  const derive = () => setTokens(deriveTokens(tokens.bg, tokens.cyan, isDark(tokens.bg) ? 'dark' : 'light'))
  const save = async () => {
    if (!name.trim()) {
      notify(t('appearance.name_required'), 'error')
      return
    }
    const saved =
      editing && theme
        ? await updateTheme({ id: theme.id, name: name.trim(), tokens })
        : await createTheme(name.trim(), tokens)
    if (!saved) return
    notify(t('appearance.theme_saved'))
    onClose()
  }
  const duplicate = async () => {
    if (!(await createTheme(`${name.trim()} ${t('appearance.copy_suffix')}`.trim(), tokens))) return
    notify(t('appearance.theme_saved'))
    onClose()
  }
  const remove = async () => {
    if (!editing || !theme) return
    if (!(await ask(t('appearance.delete_theme_confirm'), true))) return
    await deleteTheme(theme.id)
    notify(t('appearance.theme_deleted'))
    onClose()
  }
  return (
    <Dialog onClose={onClose} width={760} label={t(editing ? 'appearance.edit_theme' : 'appearance.new_theme')}>
      <DialogTitle class="mb-1">{t(editing ? 'appearance.edit_theme' : 'appearance.new_theme')}</DialogTitle>
      <div class="grid gap-3.5 lg:grid-cols-[minmax(0,1fr)_260px]">
        <div class="min-w-0">
          <div class="grid gap-2.5 sm:grid-cols-2">
            <label class="grid gap-1">
              <span class="text-xs font-semibold">{t('appearance.theme_name')}</span>
              <Input value={name} maxLength={64} onInput={e => setName(e.currentTarget.value)} autofocus />
            </label>
            <label class="grid gap-1">
              <span class="text-xs font-semibold">{t('appearance.preset')}</span>
              <Select value={preset} onChange={e => applyPreset(e.currentTarget.value)}>
                <option value="">{t('appearance.preset_choose')}</option>
                {THEME_PRESETS.map(item => (
                  <option key={item.id} value={item.id}>
                    {item.name}
                  </option>
                ))}
              </Select>
            </label>
          </div>
          <div class="mt-2 flex flex-wrap gap-2">
            <Button onClick={derive}>
              <Wand2 size={16} />
              {t('appearance.derive_from_background')}
            </Button>
            <Button onClick={() => setTokens({ ...BUILTIN_TOKENS.dark })}>
              <RotateCcw size={16} />
              {t('appearance.reset_colours')}
            </Button>
          </div>
          {ColorGroups.map(group => (
            <div key={group.title} class="mt-3">
              <h3 class="mb-1 text-base font-semibold">{t(group.title)}</h3>
              <div class="grid gap-2 sm:grid-cols-2">
                {group.tokens.map(token => (
                  <div key={token} class="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-2">
                    <span class="truncate text-sm text-muted">{t(TokenLabels[token])}</span>
                    <ColorField
                      id={`token-${token}`}
                      label={TokenLabels[token]}
                      value={tokens[token]}
                      onChange={v => set(token, v)}
                    />
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>
        <div class="min-w-0">
          <h3 class="mb-1 text-base font-semibold">{t('appearance.live_preview')}</h3>
          <MiniPreview tokens={tokens} />
          <ContrastList tokens={tokens} />
        </div>
      </div>
      <div class="mt-4 flex flex-wrap justify-end gap-2 border-t border-edge pt-3">
        {editing && (
          <>
            <Button variant="danger" onClick={() => void remove()}>
              <Trash2 size={16} />
              {t('common.delete')}
            </Button>
            <Button onClick={() => void duplicate()}>
              <Copy size={16} />
              {t('appearance.duplicate')}
            </Button>
          </>
        )}
        <Button onClick={onClose}>{t('common.cancel')}</Button>
        <Button variant="primary" onClick={() => void save()}>
          <Check size={16} />
          {t('appearance.save_theme')}
        </Button>
      </div>
    </Dialog>
  )
}

const ThemeCard = ({
  id,
  name,
  tokens,
  custom,
  onEdit,
}: {
  id: string
  name: string
  tokens: ThemeTokens
  custom?: CustomTheme
  onEdit?: () => void
}) => {
  const selected = appearance.use().doc.dashboard.theme === id
  return (
    <div
      class={cn(
        'flex min-w-0 flex-col rounded-lg border bg-panel p-3',
        selected ? 'border-cyan shadow-glow' : 'border-edge'
      )}
    >
      <button type="button" class="text-left" aria-pressed={selected} onClick={() => selectTheme(id)}>
        <div class="mb-2 flex items-center gap-2">
          <span class="truncate font-semibold">{name}</span>
          {selected && (
            <Tag tone="ok">
              <Dot c="green" /> {t('appearance.active')}
            </Tag>
          )}
        </div>
        <div class="flex h-10 overflow-hidden rounded-md border" style={{ borderColor: tokens.edge }}>
          {(['bg', 'panel', 'field', 'cyan', 'green', 'amber', 'red'] as ThemeToken[]).map(token => (
            <i key={token} class="flex-1" style={{ background: tokens[token] }} />
          ))}
        </div>
      </button>
      <div class="mt-2 flex min-h-8 items-center gap-1.5">
        <small class="mr-auto text-xs text-muted">{custom ? t('appearance.custom') : t('appearance.built_in')}</small>
        {onEdit && (
          <Button class="min-h-8 px-2 text-xs" onClick={onEdit}>
            {t('dashboard.edit')}
          </Button>
        )}
        <Button
          class="min-h-8 px-2 text-xs"
          onClick={() => downloadJson(`cc2-theme-${id}.json`, { format: 'cc2-theme', version: 1, name, tokens })}
        >
          <Download size={14} />
          {t('appearance.export')}
        </Button>
      </div>
    </div>
  )
}

const Themes = () => {
  const { doc } = appearance.use()
  const [editor, setEditor] = useState<CustomTheme | null | undefined>(undefined)
  const [systemLight, setSystemLight] = useState(() => matchMedia('(prefers-color-scheme: light)').matches)
  useEffect(() => {
    const query = matchMedia('(prefers-color-scheme: light)')
    const update = () => setSystemLight(query.matches)
    query.addEventListener?.('change', update)
    return () => query.removeEventListener?.('change', update)
  }, [])
  const custom = doc.themes
  return (
    <div class="grid gap-3.5">
      {editor !== undefined && <ThemeEditor theme={editor} onClose={() => setEditor(undefined)} />}
      <Card>
        <CardHead
          icon="sparkles"
          title="appearance.themes"
          end={
            <div class="flex gap-1.5">
              <Button class="min-h-8 px-2 text-xs" onClick={() => setEditor(null)}>
                <Plus size={14} />
                {t('appearance.new_theme')}
              </Button>
              <Button
                class="min-h-8 px-2 text-xs"
                onClick={() =>
                  setEditor({
                    id: '',
                    name: `${themeLabel(doc.dashboard.theme)} ${t('appearance.copy_suffix')}`,
                    tokens: { ...effectiveTokens() },
                  })
                }
              >
                <Copy size={14} />
                {t('appearance.duplicate')}
              </Button>
            </div>
          }
        />
        <div class="grid gap-2.5 sm:grid-cols-2 xl:grid-cols-3">
          <ThemeCard
            id="auto"
            name={t('appearance.system_theme')}
            tokens={systemLight ? BUILTIN_TOKENS.light : BUILTIN_TOKENS.dark}
          />
          {BUILTIN_THEME_IDS.map(id => (
            <ThemeCard key={id} id={id} name={themeLabel(id)} tokens={BUILTIN_TOKENS[id]} />
          ))}
          {custom.map(theme => (
            <ThemeCard
              key={theme.id}
              id={theme.id}
              name={theme.name}
              tokens={theme.tokens}
              custom={theme}
              onEdit={() => setEditor(theme)}
            />
          ))}
        </div>
        {custom.length >= 8 && <p class="mt-2 text-xs text-amber">{t('appearance.theme_limit')}</p>}
      </Card>
      <Card>
        <CardHead icon="palette" title="appearance.suggested_presets" />
        <div class="grid gap-2 sm:grid-cols-2 xl:grid-cols-4">
          {THEME_PRESETS.map(preset => (
            <button
              key={preset.id}
              type="button"
              class="rounded-lg border border-edge bg-panel p-2 text-left hover:border-cyan"
              onClick={() => setEditor({ id: '', name: preset.name, tokens: { ...preset.tokens } })}
            >
              <div class="flex h-8 overflow-hidden rounded border border-edge">
                {(['bg', 'panel', 'field', 'cyan', 'green', 'amber', 'red'] as ThemeToken[]).map(token => (
                  <i key={token} class="flex-1" style={{ background: preset.tokens[token] }} />
                ))}
              </div>
              <span class="mt-1.5 block truncate text-sm">{preset.name}</span>
            </button>
          ))}
        </div>
      </Card>
    </div>
  )
}

const LayoutPreview = ({ layout }: { layout: Layout }) => {
  const enabled = layout.widgets.filter(widget => widget.enabled)
  return (
    <fieldset
      class={cn(
        'grid gap-1.5 rounded-lg border border-edge bg-bg p-2',
        layout.columns === 2 ? 'grid-cols-2' : 'grid-cols-4'
      )}
      aria-label={t('appearance.layout_preview')}
    >
      {enabled.map(widget => (
        <div
          key={widget.id}
          class="flex min-h-10 min-w-0 items-center justify-center rounded border border-edge bg-panel p-1 text-center text-xs text-muted"
          style={{ gridColumn: `span ${widget.span}` }}
        >
          <span class="truncate">{t(WIDGET_TITLE[widget.id] as Key)}</span>
        </div>
      ))}
      {!enabled.length && (
        <p class="col-span-full p-3 text-center text-sm text-muted">{t('appearance.no_widgets_enabled')}</p>
      )}
    </fieldset>
  )
}

const WidgetEditor = () => {
  const { doc } = appearance.use()
  const [dragging, setDragging] = useState<WidgetId | null>(null)
  const layout = doc.dashboard.layouts.find(item => item.id === doc.dashboard.layout)
  if (!layout || layout.id === 'classic') {
    return (
      <Card>
        <CardHead icon="layout" title="appearance.widgets" />
        <p class="mb-3 text-sm text-muted">{t('appearance.widgets_classic_hint')}</p>
        <Button
          variant="primary"
          onClick={() => {
            newLayout(t('appearance.custom_layout_default_name'))
            notify(t('appearance.layout_created'))
          }}
        >
          <Plus size={16} />
          {t('appearance.create_custom_layout')}
        </Button>
      </Card>
    )
  }
  return (
    <Card>
      <CardHead icon="layout" title="appearance.widgets" />
      <ul class="m-0 grid list-none gap-2 p-0">
        {layout.widgets.map((widget, index) => (
          <li
            key={widget.id}
            data-widget={widget.id}
            draggable
            onDragStart={() => setDragging(widget.id)}
            onDragEnd={() => setDragging(null)}
            onDragOver={event => event.preventDefault()}
            onDrop={() => {
              if (dragging) dropWidget(dragging, widget.id)
              setDragging(null)
            }}
            class={cn(
              'grid items-center gap-2 rounded-md border border-edge bg-field p-2',
              'grid-cols-[auto_minmax(0,1fr)_auto_auto]',
              dragging === widget.id && 'opacity-60'
            )}
          >
            <span class="cursor-grab text-muted" aria-hidden="true" title={t('appearance.drag_to_reorder')}>
              <GripVertical size={18} />
            </span>
            <span class="min-w-0 truncate font-semibold">{t(WIDGET_TITLE[widget.id] as Key)}</span>
            <Switch
              on={widget.enabled}
              label={t(WIDGET_TITLE[widget.id] as Key)}
              onClick={() =>
                updateLayout(item => {
                  const found = item.widgets.find(w => w.id === widget.id)
                  if (found) found.enabled = !found.enabled
                })
              }
            />
            <div class="flex items-center gap-1">
              <button
                type="button"
                class="rounded p-1.5 text-muted hover:bg-panel hover:text-fg disabled:opacity-40"
                aria-label={tpl('appearance.move_widget_up', { name: t(WIDGET_TITLE[widget.id] as Key) })}
                disabled={index === 0}
                onClick={event => {
                  const label = event.currentTarget.getAttribute('aria-label')
                  moveWidget(widget.id, -1)
                  requestAnimationFrame(() => {
                    const buttons = document.querySelectorAll<HTMLButtonElement>(`[data-widget="${widget.id}"] button`)
                    for (const button of buttons) if (button.getAttribute('aria-label') === label) button.focus()
                  })
                }}
              >
                <ArrowUp size={16} />
              </button>
              <button
                type="button"
                class="rounded p-1.5 text-muted hover:bg-panel hover:text-fg disabled:opacity-40"
                aria-label={tpl('appearance.move_widget_down', { name: t(WIDGET_TITLE[widget.id] as Key) })}
                disabled={index === layout.widgets.length - 1}
                onClick={event => {
                  const label = event.currentTarget.getAttribute('aria-label')
                  moveWidget(widget.id, 1)
                  requestAnimationFrame(() => {
                    const buttons = document.querySelectorAll<HTMLButtonElement>(`[data-widget="${widget.id}"] button`)
                    for (const button of buttons) if (button.getAttribute('aria-label') === label) button.focus()
                  })
                }}
              >
                <ArrowDown size={16} />
              </button>
            </div>
            <div class="col-start-2 col-end-5 grid gap-2 border-t border-edge pt-2 sm:grid-cols-[110px_140px_auto]">
              <label class="grid gap-0.5 text-xs">
                <span class="text-muted">{t('appearance.widget_span')}</span>
                <Select
                  value={String(widget.span)}
                  onChange={e =>
                    updateLayout(item => {
                      const found = item.widgets.find(w => w.id === widget.id)
                      if (found) found.span = Number(e.currentTarget.value) as 1 | 2 | 4
                    })
                  }
                >
                  <option value="1">{t('appearance.widget_span_1')}</option>
                  <option value="2">{t('appearance.widget_span_2')}</option>
                  <option value="4">{t('appearance.widget_span_4')}</option>
                </Select>
              </label>
              <label class="flex items-center gap-2 self-end pb-1 text-xs">
                <Switch
                  on={widget.compact}
                  label={tpl('appearance.widget_compact_name', { name: t(WIDGET_TITLE[widget.id] as Key) })}
                  onClick={() =>
                    updateLayout(item => {
                      const found = item.widgets.find(w => w.id === widget.id)
                      if (found) found.compact = !found.compact
                    })
                  }
                />
                <span class="text-muted">{t('appearance.widget_compact')}</span>
              </label>
              <div class="grid gap-0.5 text-xs">
                <span class="text-muted">{t('appearance.widget_accent')}</span>
                <div class="flex items-center gap-1">
                  <input
                    type="color"
                    aria-label={tpl('appearance.widget_accent_name', { name: t(WIDGET_TITLE[widget.id] as Key) })}
                    value={widget.accent || effectiveTokens(doc).cyan}
                    onInput={e =>
                      updateLayout(item => {
                        const found = item.widgets.find(w => w.id === widget.id)
                        if (found) found.accent = e.currentTarget.value.toLowerCase()
                      })
                    }
                    class="size-8 rounded border border-edge bg-field p-0.5"
                  />
                  <Button
                    class="min-h-8 px-2 text-xs"
                    onClick={() =>
                      updateLayout(item => {
                        const found = item.widgets.find(w => w.id === widget.id)
                        if (found) found.accent = ''
                      })
                    }
                  >
                    {t('appearance.widget_accent_clear')}
                  </Button>
                </div>
              </div>
            </div>
          </li>
        ))}
      </ul>
      <p class="mt-2 text-xs text-muted">{t('appearance.widgets_order_hint')}</p>
    </Card>
  )
}

const LayoutTab = () => {
  const { doc } = appearance.use()
  const layout = doc.dashboard.layouts.find(item => item.id === doc.dashboard.layout)
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="layout" title="appearance.layout" />
        <FieldRow label="appearance.active_layout" htmlFor="active-layout">
          <Select
            id="active-layout"
            value={doc.dashboard.layout}
            onChange={e =>
              updateDoc(current => {
                current.dashboard.layout = e.currentTarget.value
              })
            }
          >
            {doc.dashboard.layouts.map(item => (
              <option key={item.id} value={item.id}>
                {item.id === 'classic' ? t('appearance.layout_classic') : item.name}
              </option>
            ))}
          </Select>
        </FieldRow>
        {layout && layout.id === 'classic' ? (
          <p class="text-sm text-muted">{t('appearance.layout_classic_hint')}</p>
        ) : (
          layout && (
            <>
              <FieldRow label="appearance.layout_name" htmlFor="layout-name">
                <Input
                  id="layout-name"
                  value={layout.name}
                  maxLength={64}
                  onInput={e => {
                    const value = e.currentTarget.value
                    updateLayout(item => {
                      item.name = value
                    })
                  }}
                />
              </FieldRow>
              <FieldRow label="appearance.layout_columns" htmlFor="layout-columns">
                <Select
                  id="layout-columns"
                  value={String(layout.columns)}
                  onChange={e =>
                    updateLayout(item => {
                      item.columns = Number(e.currentTarget.value) === 2 ? 2 : 4
                    })
                  }
                >
                  <option value="2">{t('appearance.columns_2')}</option>
                  <option value="4">{t('appearance.columns_4')}</option>
                </Select>
              </FieldRow>
              <LayoutPreview layout={layout} />
            </>
          )
        )}
        <div class="mt-3 flex flex-wrap gap-2 border-t border-edge pt-3">
          <Button
            onClick={() => {
              newLayout(t('appearance.custom_layout_default_name'))
              notify(t('appearance.layout_created'))
            }}
            disabled={doc.dashboard.layouts.length >= 4}
          >
            <Plus size={16} />
            {t('appearance.new_layout')}
          </Button>
          <Button
            onClick={() => duplicateLayout(doc.dashboard.layout)}
            disabled={doc.dashboard.layouts.length >= 4 || doc.dashboard.layout === 'classic'}
          >
            <Copy size={16} />
            {t('appearance.duplicate_layout')}
          </Button>
          <Button
            variant="danger"
            onClick={async () => {
              if (!(await ask(t('appearance.delete_layout_confirm'), true))) return
              deleteLayout(doc.dashboard.layout)
              notify(t('appearance.layout_deleted'))
            }}
            disabled={doc.dashboard.layout === 'classic' || doc.dashboard.layout === 'custom-grid'}
          >
            <Trash2 size={16} />
            {t('appearance.delete_layout')}
          </Button>
          <Button
            class="ml-auto"
            onClick={() => {
              updateDoc(current => {
                current.dashboard.layout = 'classic'
              })
              notify(t('appearance.layout_reset_done'))
            }}
          >
            <RotateCcw size={16} />
            {t('appearance.reset_to_classic')}
          </Button>
        </div>
        {doc.dashboard.layouts.length >= 4 && <p class="mt-2 text-xs text-amber">{t('appearance.layout_limit')}</p>}
      </Card>
      <WidgetEditor />
    </div>
  )
}

const ThemePicker = () => {
  const { doc } = appearance.use()
  return (
    <FieldRow label="appearance.theme" htmlFor="overview-theme">
      <Select id="overview-theme" value={doc.dashboard.theme} onChange={e => selectTheme(e.currentTarget.value)}>
        <option value="auto">{t('appearance.system_theme')}</option>
        {THEME_CHOICES.filter(item => item.id !== 'auto').map(item => (
          <option key={item.id} value={item.id}>
            {item.key ? t(item.key) : item.label}
          </option>
        ))}
        {doc.themes.map(theme => (
          <option key={theme.id} value={theme.id}>
            {theme.name}
          </option>
        ))}
      </Select>
    </FieldRow>
  )
}

const Overview = () => {
  const { doc, loaded, error, saving, savedAt } = appearance.use()
  const layout = doc.dashboard.layouts.find(item => item.id === doc.dashboard.layout)
  const profile = doc.profiles.find(item => item.id === doc.activeProfile)
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="palette" title="appearance.overview" />
        <p class="mb-3 max-w-3xl text-sm text-muted">{t('appearance.overview_intro')}</p>
        <div class="grid min-w-0 gap-2.5 sm:grid-cols-2">
          <div class="min-w-0 rounded-md border border-edge bg-field p-3">
            <small class="text-muted">{t('appearance.active_theme')}</small>
            <div class="mt-1 flex min-w-0 items-center gap-2">
              <span
                class="size-5 rounded-full border border-edge"
                style={{ background: effectiveTokens(doc).cyan }}
                aria-hidden="true"
              />
              <b class="min-w-0 truncate">
                {doc.dashboard.theme === 'auto'
                  ? t('appearance.system_theme')
                  : doc.dashboard.theme.startsWith('custom-')
                    ? (doc.themes.find(theme => theme.id === doc.dashboard.theme)?.name ?? doc.dashboard.theme)
                    : themeLabel(doc.dashboard.theme)}
              </b>
            </div>
          </div>
          <div class="min-w-0 rounded-md border border-edge bg-field p-3">
            <small class="text-muted">{t('appearance.active_layout')}</small>
            <div class="mt-1 truncate font-semibold">
              {layout?.id === 'classic' ? t('appearance.layout_classic') : layout?.name}
            </div>
          </div>
          <div class="min-w-0 rounded-md border border-edge bg-field p-3">
            <small class="text-muted">{t('appearance.active_profile')}</small>
            <div class="mt-1 truncate font-semibold">{profile?.name ?? doc.activeProfile}</div>
          </div>
          <div class="min-w-0 rounded-md border border-edge bg-field p-3" role="status">
            <small class="text-muted">{t('appearance.storage')}</small>
            <div class="mt-1 flex min-w-0 items-center gap-2">
              <Dot c={error ? 'red' : saving ? 'amber' : loaded ? 'green' : 'amber'} />
              <b class="min-w-0 truncate">
                {error
                  ? tpl('appearance.save_failed_error', { error })
                  : saving
                    ? t('appearance.saving')
                    : savedAt
                      ? t('appearance.saved')
                      : loaded
                        ? t('appearance.stored_on_printer')
                        : t('common.checking_configuration')}
              </b>
            </div>
          </div>
        </div>
        <div class="mt-3 grid gap-2 sm:grid-cols-2">
          <ThemePicker />
          <FieldRow label="appearance.density" htmlFor="overview-density">
            <Select
              id="overview-density"
              value={String(doc.dashboard.density)}
              onChange={e =>
                updateDoc(current => {
                  current.dashboard.density = Number(e.currentTarget.value) as 0 | 1 | 2
                })
              }
            >
              <option value="0">{t('appearance.density_comfortable')}</option>
              <option value="1">{t('appearance.density_compact')}</option>
              <option value="2">{t('appearance.density_spacious')}</option>
            </Select>
          </FieldRow>
        </div>
        <div class="mt-2 flex flex-wrap gap-2">
          <Button onClick={() => openAppearance('themes')}>
            <Sparkles size={16} />
            {t('appearance.open_themes')}
          </Button>
          <Button onClick={() => openAppearance('layout')}>
            <LayoutGrid size={16} />
            {t('appearance.open_layout')}
          </Button>
          <Button onClick={() => openAppearance('touchscreen')}>
            <Smartphone size={16} />
            {t('appearance.open_touchscreen')}
          </Button>
          <Button onClick={() => openAppearance('profiles')}>
            <UserRound size={16} />
            {t('appearance.open_profiles')}
          </Button>
        </div>
      </Card>
      <Card>
        <CardHead icon="info" title="appearance.two_systems" />
        <p class="text-sm text-muted">{t('appearance.two_systems_hint')}</p>
      </Card>
    </div>
  )
}

const ACCENT_PRESETS = ['#7eead8', '#4da3ff', '#b28aff', '#ff7ab6', '#ffb020', '#5fd068', '#ff5a54']
const BACKGROUND_PRESETS = ['#0a0c11', '#0b1220', '#101418', '#151015', '#071410', '#121a12']

const Touchscreen = () => {
  const { doc } = appearance.use()
  const { status, loading, error, progress, busy } = display.use()
  const [stage, setStage] = useState('')
  const [file, setFile] = useState<File | null>(null)
  const [fileName, setFileName] = useState('')
  const fileInput = useRef<HTMLInputElement>(null)
  useEffect(() => {
    void loadDisplay()
  }, [])
  usePoll(loadDisplay, 15000)
  const touch = doc.touchscreen
  const setTouch = (change: (touch: typeof doc.touchscreen) => void) =>
    updateDoc(current => change(current.touchscreen))
  const chooseFile = (selected: File | null) => {
    if (selected && selected.size > 8 * 1024 * 1024) {
      notify(t('appearance.boot_file_too_large'), 'error')
      return
    }
    setFile(selected)
    setFileName(selected?.name ?? '')
  }
  const apply = async () => {
    if (!status?.supported) return
    if (touch.boot === 'custom' && !file) {
      notify(t('appearance.boot_choose_first'), 'error')
      return
    }
    if (!(await ask(t('appearance.apply_confirm'), true))) return
    display.set({ busy: true, progress: 0 })
    try {
      const pack = await buildPack(
        status,
        { accent: touch.accent, background: touch.background, boot: touch.boot, bootArt: file },
        text => setStage(text)
      )
      await applyPack(pack, percent => display.set({ progress: percent }))
      notify(t('appearance.touchscreen_applied'))
      await loadDisplay()
    } catch (reason) {
      notify(tpl('appearance.touchscreen_failed_error', { error: errText(reason) }), 'error')
    } finally {
      display.set({ busy: false })
      setStage('')
    }
  }
  const restore = async () => {
    if (!(await ask(t('appearance.restore_confirm'), true))) return
    display.set({ busy: true })
    try {
      await restoreDisplay()
      notify(t('appearance.touchscreen_restored'))
      await loadDisplay()
    } catch (reason) {
      notify(tpl('appearance.touchscreen_failed_error', { error: errText(reason) }), 'error')
    } finally {
      display.set({ busy: false })
    }
  }
  const stageLabel =
    stage === 'accent'
      ? t('appearance.accent')
      : stage === 'background'
        ? t('appearance.background')
        : stage === 'boot'
          ? t('appearance.boot_art')
          : stage === 'pack'
            ? t('appearance.preparing')
            : ''
  return (
    <div class="grid gap-3.5">
      {error && (
        <Card>
          <CardHead icon="info" title="appearance.touchscreen_status" />
          <p class="text-sm text-red" role="alert">
            {tpl('appearance.touchscreen_failed_error', { error })}
          </p>
        </Card>
      )}
      <Card>
        <CardHead
          icon="smartphone"
          title="appearance.touchscreen"
          end={
            loading ? (
              <span class="text-xs text-muted">{t('common.checking_configuration')}</span>
            ) : status?.supported ? (
              <Tag tone="ok">
                <Dot c="green" /> {t('appearance.supported')}
              </Tag>
            ) : (
              <Tag tone="warning">
                <Dot c="amber" /> {t('appearance.not_supported')}
              </Tag>
            )
          }
        />
        {!status && loading && <p class="text-sm text-muted">{t('common.checking_configuration')}</p>}
        {status && !status.supported && (
          <div class="rounded-md border border-amber/50 bg-amber/10 p-3">
            <p class="text-sm text-amber" role="status">
              {t(reasonKey(status.error) as Key)}
            </p>
            <p class="mt-2 text-xs text-muted">{t('appearance.touchscreen_unsupported_hint')}</p>
          </div>
        )}
        {status?.supported && (
          <>
            <div class="mb-3 grid gap-2 sm:grid-cols-3">
              <div class="rounded-md border border-edge bg-field p-2.5">
                <small class="text-muted">{t('appearance.display_firmware')}</small>
                <div class="truncate font-semibold">{status.firmwareBase || '—'}</div>
              </div>
              <div class="rounded-md border border-edge bg-field p-2.5">
                <small class="text-muted">{t('appearance.display_files')}</small>
                <div class="font-semibold tabular-nums">
                  {status.customizedCount} / {status.fileCount}
                </div>
              </div>
              <div class="rounded-md border border-edge bg-field p-2.5">
                <small class="text-muted">{t('appearance.display_backup')}</small>
                <div class="font-semibold">
                  {status.backupAvailable ? t('appearance.backup_available') : t('appearance.backup_missing')}
                </div>
              </div>
            </div>
            {status.recoveryRequired && (
              <p class="mb-3 rounded-md border border-red/50 bg-red/10 p-2.5 text-sm text-red" role="alert">
                {t('appearance.recovery_required')}
              </p>
            )}
            {status.rebootRequired && (
              <p class="mb-3 rounded-md border border-amber/50 bg-amber/10 p-2.5 text-sm text-amber" role="status">
                {t('appearance.reboot_required')}
              </p>
            )}
            <fieldset class="mb-3 rounded-md border border-edge p-2.5">
              <legend class="px-1 text-sm font-semibold">{t('appearance.experimental_access')}</legend>
              <div class="flex items-start gap-2 text-sm">
                <Switch
                  on={touch.experimental}
                  label={t('appearance.experimental_access')}
                  onClick={() => setTouch(item => (item.experimental = !item.experimental))}
                />
                <span>
                  {t('appearance.experimental_access_hint')}
                  <span class="mt-1 block text-xs text-muted">{t('appearance.experimental_access_note')}</span>
                </span>
              </div>
            </fieldset>
            <fieldset class="mb-3 rounded-md border border-edge p-2.5" disabled={!touch.experimental}>
              <legend class="px-1 text-sm font-semibold">{t('appearance.touchscreen_appearance')}</legend>
              <div class="mb-2 flex items-center gap-2 text-sm">
                <Switch
                  on={touch.linkAccent}
                  label={t('appearance.link_accent')}
                  onClick={() => setTouch(item => (item.linkAccent = !item.linkAccent))}
                />
                {t('appearance.link_accent_hint')}
              </div>
              <div class="grid gap-2 sm:grid-cols-2">
                <div>
                  <span class="text-xs text-muted">{t('appearance.accent')}</span>
                  <div class="mt-1 flex flex-wrap items-center gap-1.5">
                    {ACCENT_PRESETS.map(colour => (
                      <button
                        key={colour}
                        type="button"
                        class={cn(
                          'size-7 rounded-full border-2',
                          touch.accent === colour ? 'border-fg' : 'border-transparent'
                        )}
                        style={{ background: colour }}
                        aria-label={tpl('appearance.use_accent', { colour })}
                        aria-pressed={touch.accent === colour}
                        disabled={touch.linkAccent}
                        onClick={() => setTouch(item => (item.accent = colour))}
                      />
                    ))}
                    <ColorField
                      id="touch-accent"
                      label="appearance.accent"
                      value={touch.accent}
                      disabled={touch.linkAccent}
                      onChange={colour => setTouch(item => (item.accent = colour))}
                    />
                  </div>
                </div>
                <div>
                  <span class="text-xs text-muted">{t('appearance.background')}</span>
                  <div class="mt-1 flex flex-wrap items-center gap-1.5">
                    {BACKGROUND_PRESETS.map(colour => (
                      <button
                        key={colour}
                        type="button"
                        class={cn(
                          'size-7 rounded-md border-2',
                          touch.background === colour ? 'border-fg' : 'border-edge'
                        )}
                        style={{ background: colour }}
                        aria-label={tpl('appearance.use_background', { colour })}
                        aria-pressed={touch.background === colour}
                        onClick={() => setTouch(item => (item.background = colour))}
                      />
                    ))}
                    <ColorField
                      id="touch-background"
                      label="appearance.background"
                      value={touch.background}
                      onChange={colour => setTouch(item => (item.background = colour))}
                    />
                  </div>
                  <p class="mt-1 text-xs text-muted">{t('appearance.background_dark_hint')}</p>
                </div>
              </div>
              <div class="mt-2 grid gap-1">
                <span class="text-xs text-muted">{t('appearance.boot_art')}</span>
                <div class="flex flex-wrap items-center gap-2">
                  <Select
                    class="max-w-64"
                    value={touch.boot}
                    onChange={e =>
                      setTouch(item => (item.boot = e.currentTarget.value === 'custom' ? 'custom' : 'community'))
                    }
                  >
                    <option value="community">{t('appearance.boot_community')}</option>
                    <option value="custom">{t('appearance.boot_custom')}</option>
                  </Select>
                  {touch.boot === 'custom' && (
                    <>
                      <input
                        ref={fileInput}
                        type="file"
                        accept="image/png,image/jpeg,image/webp"
                        class="hidden"
                        onChange={e => chooseFile(e.currentTarget.files?.[0] ?? null)}
                      />
                      <Button onClick={() => fileInput.current?.click()}>
                        <Upload size={16} />
                        {t('appearance.boot_choose')}
                      </Button>
                      {fileName ? (
                        <span class="flex min-w-0 items-center gap-1 text-xs text-muted">
                          <Check size={14} class="text-green" />
                          <span class="max-w-52 truncate">{fileName}</span>
                          <button type="button" class="underline" onClick={() => chooseFile(null)}>
                            {t('common.cancel')}
                          </button>
                        </span>
                      ) : (
                        <span class="text-xs text-muted">{t('appearance.boot_none')}</span>
                      )}
                    </>
                  )}
                </div>
                <p class="text-xs text-muted">{t('appearance.boot_hint')}</p>
              </div>
            </fieldset>
            {busy && (
              <div class="mb-3" role="status">
                <p class="text-sm text-muted">
                  {stageLabel ? `${stageLabel} · ` : ''}
                  {progress ? tpl('appearance.upload_percent', { percent: progress }) : t('appearance.preparing')}
                </p>
                <progress
                  class="h-1.5 w-full"
                  max={100}
                  value={progress}
                  aria-label={t('appearance.upload_progress')}
                />
              </div>
            )}
            <div class="flex flex-wrap gap-2 border-t border-edge pt-3">
              <Button variant="primary" disabled={!touch.experimental || busy} onClick={() => void apply()}>
                <Check size={16} />
                {t('appearance.apply_touchscreen')}
              </Button>
            </div>
          </>
        )}
      </Card>
      {status?.restoreAvailable && (
        <Card>
          <CardHead
            icon="restore"
            title="appearance.restore_touchscreen"
            end={
              status.firmwareChanged ? (
                <Tag tone="warning">
                  <Dot c="amber" /> {t('appearance.firmware_changed')}
                </Tag>
              ) : undefined
            }
          />
          <p class="mb-3 text-sm text-muted">
            {status.supported ? t('appearance.restore_hint') : t('appearance.restore_without_manifest')}
          </p>
          {status.firmwareChanged && (
            <p class="mb-3 rounded-md border border-amber/50 bg-amber/10 p-2.5 text-sm text-amber" role="alert">
              {t('appearance.firmware_changed_warning')}
            </p>
          )}
          <Button variant="danger" disabled={busy} onClick={() => void restore()}>
            <RotateCcw size={16} />
            {t('appearance.restore_touchscreen')}
          </Button>
        </Card>
      )}
      <Card>
        <CardHead icon="info" title="appearance.touchscreen_limits" />
        <p class="text-sm text-muted">{t('appearance.limit_text')}</p>
      </Card>
    </div>
  )
}

const Profiles = () => {
  const { doc } = appearance.use()
  const [name, setName] = useState('')
  const [editing, setEditing] = useState<string | null>(null)
  const profile = doc.profiles.find(item => item.id === doc.activeProfile)
  const create = async () => {
    if (!name.trim()) {
      notify(t('appearance.name_required'), 'error')
      return
    }
    if (await saveProfile(name.trim())) {
      notify(t('appearance.profile_saved'))
      setName('')
    }
  }
  const rename = async () => {
    if (!editing || !name.trim()) return
    if (await saveProfile(name.trim(), editing)) {
      notify(t('appearance.profile_saved'))
      setEditing(null)
      setName('')
    }
  }
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="profiles" title="appearance.profiles" />
        <p class="mb-3 text-sm text-muted">{t('appearance.profiles_intro')}</p>
        <div class="grid gap-2">
          {doc.profiles.map(item => (
            <div
              key={item.id}
              class={cn(
                'flex min-w-0 flex-wrap items-center gap-2 rounded-md border p-2.5',
                item.id === doc.activeProfile ? 'border-cyan bg-cyan/5' : 'border-edge bg-field'
              )}
            >
              <div class="min-w-0 flex-1">
                <div class="flex min-w-0 items-center gap-2">
                  <b class="min-w-0 truncate">{item.name}</b>
                  {item.id === 'community' && <Tag>{t('appearance.built_in')}</Tag>}
                  {item.id === doc.activeProfile && (
                    <Tag tone="ok">
                      <Dot c="green" /> {t('appearance.active')}
                    </Tag>
                  )}
                </div>
                <small class="text-muted">
                  {themeLabel(item.dashboard.theme)} ·{' '}
                  {item.dashboard.layout === 'classic'
                    ? t('appearance.layout_classic')
                    : (item.dashboard.layouts.find(layout => layout.id === item.dashboard.layout)?.name ??
                      item.dashboard.layout)}
                </small>
              </div>
              {item.id !== doc.activeProfile && (
                <Button class="min-h-8 px-2 text-xs" onClick={() => void applyProfile(item.id)}>
                  <ChevronRight size={14} />
                  {t('appearance.profile_apply')}
                </Button>
              )}
              <Button
                class="min-h-8 px-2 text-xs"
                onClick={() => {
                  setEditing(item.id)
                  setName(item.name)
                }}
              >
                {t('dashboard.edit')}
              </Button>
              <Button
                class="min-h-8 px-2 text-xs"
                disabled={doc.profiles.length >= 6}
                onClick={async () => {
                  if (await duplicateProfile(item.id)) notify(t('appearance.profile_saved'))
                }}
              >
                <Copy size={14} />
                {t('appearance.duplicate')}
              </Button>
              <Button
                class="min-h-8 px-2 text-xs"
                onClick={() =>
                  downloadJson(`cc2-profile-${item.id}.json`, {
                    format: 'cc2-profile',
                    version: 1,
                    profile: item,
                    theme: doc.themes.find(theme => theme.id === item.dashboard.theme) ?? null,
                  })
                }
              >
                <Download size={14} />
                {t('appearance.export')}
              </Button>
              {item.id !== 'community' && (
                <Button
                  class="min-h-8 px-2 text-xs"
                  variant="danger"
                  aria-label={tpl('appearance.delete_profile_name', { name: item.name })}
                  onClick={async () => {
                    if (!(await ask(t('appearance.delete_profile_confirm'), true))) return
                    if (await removeProfile(item.id)) notify(t('appearance.profile_deleted'))
                  }}
                  disabled={item.id === doc.activeProfile}
                >
                  <Trash2 size={14} />
                </Button>
              )}
            </div>
          ))}
        </div>
      </Card>
      <Card>
        <CardHead icon="save" title="appearance.save_profile" />
        <div class="grid gap-2 sm:grid-cols-[minmax(0,1fr)_auto]">
          <Input
            value={name}
            maxLength={64}
            placeholder={profile?.name ?? t('appearance.profile_name')}
            aria-label={t('appearance.profile_name')}
            onInput={e => setName(e.currentTarget.value)}
          />
          {editing ? (
            <div class="flex gap-2">
              <Button onClick={() => void rename()}>
                <Save size={16} />
                {t('appearance.save_profile_action')}
              </Button>
              <Button
                onClick={() => {
                  setEditing(null)
                  setName('')
                }}
              >
                {t('common.cancel')}
              </Button>
            </div>
          ) : (
            <Button variant="primary" onClick={() => void create()} disabled={doc.profiles.length >= 6}>
              <Plus size={16} />
              {t('appearance.save_current_as_profile')}
            </Button>
          )}
        </div>
        {doc.profiles.length >= 6 && <p class="mt-2 text-xs text-amber">{t('appearance.profile_limit')}</p>}
      </Card>
    </div>
  )
}

const readFile = (file: File) =>
  new Promise<string>((resolve, reject) => {
    if (file.size > 256 * 1024) {
      reject(Error(t('appearance.import_too_large')))
      return
    }
    const reader = new FileReader()
    reader.onerror = () => reject(Error(t('appearance.import_bad')))
    reader.onload = () => resolve(String(reader.result ?? ''))
    reader.readAsText(file)
  })

const Transfer = () => {
  const { doc } = appearance.use()
  const [themeId, setThemeId] = useState(doc.themes[0]?.id ?? '')
  const fullInput = useRef<HTMLInputElement>(null)
  const themeInput = useRef<HTMLInputElement>(null)
  const importTheme = async (file: File) => {
    try {
      const parsed = JSON.parse(await readFile(file))
      if (parsed?.format !== 'cc2-theme' || typeof parsed.name !== 'string' || !parsed.tokens) throw Error()
      const valid = normalizeThemes([{ id: 'custom-imported', name: parsed.name, tokens: parsed.tokens }])[0]
      if (!valid) throw Error()
      const theme = await createTheme(valid.name, valid.tokens, false)
      if (theme) notify(t('appearance.import_success'))
    } catch {
      notify(t('appearance.import_bad'), 'error')
    }
  }
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="transfer" title="appearance.transfer" />
        <p class="mb-3 text-sm text-muted">{t('appearance.transfer_intro')}</p>
        <div class="flex flex-wrap gap-2">
          <Button onClick={() => downloadJson('cc2-appearance.json', exportAppearance())}>
            <Download size={16} />
            {t('appearance.export_all')}
          </Button>
          <input
            ref={fullInput}
            type="file"
            accept="application/json,.json"
            class="hidden"
            onChange={async e => {
              const file = e.currentTarget.files?.[0]
              e.currentTarget.value = ''
              if (!file) return
              try {
                if (await importAppearance(JSON.parse(await readFile(file)))) notify(t('appearance.import_success'))
              } catch {
                notify(t('appearance.import_bad'), 'error')
              }
            }}
          />
          <Button onClick={() => fullInput.current?.click()}>
            <Upload size={16} />
            {t('appearance.import_all')}
          </Button>
        </div>
        <p class="mt-2 text-xs text-muted">{t('appearance.transfer_note')}</p>
      </Card>
      <Card>
        <CardHead icon="sparkles" title="appearance.theme_transfer" />
        <div class="flex flex-wrap items-end gap-2">
          <label class="grid gap-1 text-xs">
            <span class="text-muted">{t('appearance.custom_theme')}</span>
            <Select value={themeId} onChange={e => setThemeId(e.currentTarget.value)} class="min-w-52">
              {doc.themes.map(theme => (
                <option key={theme.id} value={theme.id}>
                  {theme.name}
                </option>
              ))}
              {!doc.themes.length && <option value="">{t('appearance.no_custom_themes')}</option>}
            </Select>
          </label>
          <Button
            disabled={!doc.themes.length}
            onClick={() => {
              const theme = doc.themes.find(item => item.id === themeId)
              if (theme)
                downloadJson(`cc2-theme-${theme.id}.json`, {
                  format: 'cc2-theme',
                  version: 1,
                  name: theme.name,
                  tokens: theme.tokens,
                })
            }}
          >
            <Download size={16} />
            {t('appearance.export_theme')}
          </Button>
          <input
            ref={themeInput}
            type="file"
            accept="application/json,.json"
            class="hidden"
            onChange={async e => {
              const file = e.currentTarget.files?.[0]
              e.currentTarget.value = ''
              if (file) await importTheme(file)
            }}
          />
          <Button onClick={() => themeInput.current?.click()}>
            <Upload size={16} />
            {t('appearance.import_theme')}
          </Button>
        </div>
        <p class="mt-2 text-xs text-muted">{t('appearance.theme_transfer_note')}</p>
      </Card>
    </div>
  )
}

const Advanced = () => {
  const { doc, error, savedAt } = appearance.use()
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="advanced" title="appearance.advanced" />
        <FieldRow label="appearance.density" htmlFor="adv-density">
          <Select
            id="adv-density"
            value={String(doc.dashboard.density)}
            onChange={e =>
              updateDoc(current => {
                current.dashboard.density = Number(e.currentTarget.value) as 0 | 1 | 2
              })
            }
          >
            <option value="0">{t('appearance.density_comfortable')}</option>
            <option value="1">{t('appearance.density_compact')}</option>
            <option value="2">{t('appearance.density_spacious')}</option>
          </Select>
        </FieldRow>
        <FieldRow label="appearance.font" htmlFor="adv-font">
          <Select
            id="adv-font"
            value={String(doc.dashboard.font)}
            onChange={e =>
              updateDoc(current => {
                current.dashboard.font = Number(e.currentTarget.value) as 0 | 1 | 2
              })
            }
          >
            <option value="0">{t('appearance.font_inter')}</option>
            <option value="1">{t('appearance.font_system')}</option>
            <option value="2">{t('appearance.font_mono')}</option>
          </Select>
        </FieldRow>
        <FieldRow label="appearance.font_scale" htmlFor="adv-scale">
          <div class="flex items-center gap-3">
            <input
              id="adv-scale"
              type="range"
              min="0.85"
              max="1.3"
              step="0.05"
              value={doc.dashboard.scale}
              onInput={e =>
                updateDoc(current => {
                  current.dashboard.scale = Number(e.currentTarget.value)
                }, false)
              }
              onChange={() => void saveAppearance().catch(error => notify(errText(error), 'error'))}
              class="min-w-0 flex-1"
            />
            <b class="w-12 text-right tabular-nums">{Math.round(doc.dashboard.scale * 100)}%</b>
          </div>
        </FieldRow>
        <FieldRow label="appearance.corner_radius" htmlFor="adv-radius">
          <div class="flex items-center gap-3">
            <input
              id="adv-radius"
              type="range"
              min="0"
              max="24"
              step="1"
              value={doc.dashboard.radius}
              onInput={e =>
                updateDoc(current => {
                  current.dashboard.radius = Number(e.currentTarget.value)
                }, false)
              }
              onChange={() => void saveAppearance().catch(error => notify(errText(error), 'error'))}
              class="min-w-0 flex-1"
            />
            <b class="w-12 text-right tabular-nums">{doc.dashboard.radius}px</b>
          </div>
        </FieldRow>
        <FieldRow label="appearance.spacing" htmlFor="adv-space">
          <Select
            id="adv-space"
            value={String(doc.dashboard.spacing)}
            onChange={e =>
              updateDoc(current => {
                current.dashboard.spacing = Number(e.currentTarget.value) as 0 | 1 | 2
              })
            }
          >
            <option value="0">{t('appearance.spacing_tight')}</option>
            <option value="1">{t('appearance.spacing_normal')}</option>
            <option value="2">{t('appearance.spacing_roomy')}</option>
          </Select>
        </FieldRow>
        <FieldRow label="appearance.animations">
          <Switch
            on={doc.dashboard.animations}
            label={t('appearance.animations')}
            onClick={() =>
              updateDoc(current => {
                current.dashboard.animations = !current.dashboard.animations
              })
            }
          />
        </FieldRow>
        <div class="mt-3 rounded-md border border-edge bg-field p-2.5 text-xs text-muted">
          {t('appearance.safe_customization')}
        </div>
      </Card>
      <Card>
        <CardHead icon="restore" title="appearance.reset" />
        <p class="mb-3 text-sm text-muted">{t('appearance.reset_hint')}</p>
        <div class="flex flex-wrap gap-2">
          <Button
            onClick={async () => {
              if (!(await ask(t('appearance.reset_dashboard_confirm'), true))) return
              updateDoc(current => {
                current.dashboard = DEFAULT_DASHBOARD()
              })
              applyAppearance()
              notify(t('appearance.reset_done'))
            }}
          >
            <RotateCcw size={16} />
            {t('appearance.reset_dashboard')}
          </Button>
          <Button
            variant="danger"
            onClick={async () => {
              if (!(await ask(t('appearance.reset_all_confirm'), true))) return
              if (await resetAppearance(true)) {
                applyAppearance()
                notify(t('appearance.reset_done'))
              }
            }}
          >
            <RotateCcw size={16} />
            {t('appearance.reset_all')}
          </Button>
        </div>
        <dl class="mt-3 grid max-w-md grid-cols-[150px_1fr] gap-1 text-xs">
          <dt class="text-muted">{t('appearance.schema')}</dt>
          <dd class="font-mono">v{doc.version}</dd>
          <dt class="text-muted">{t('appearance.profiles_count')}</dt>
          <dd>{doc.profiles.length} / 6</dd>
          <dt class="text-muted">{t('appearance.themes_count')}</dt>
          <dd>{doc.themes.length} / 8</dd>
          <dt class="text-muted">{t('appearance.last_save')}</dt>
          <dd>{error ? t('appearance.save_failed') : savedAt ? new Date(savedAt).toLocaleTimeString() : '—'}</dd>
        </dl>
      </Card>
    </div>
  )
}

export const Appearance = () => {
  const tab = nav.use().appearance
  const { error, doc } = appearance.use()
  useEffect(() => {
    if (!appearance.get().loaded) void loadAppearance()
  }, [])
  return (
    <div class="cc2-appearance mx-auto max-w-5xl">
      <Page
        title="appearance.title"
        sub="appearance.subtitle"
        tags={
          <div class="flex items-center gap-2 text-xs text-muted">
            <Dot c={error ? 'red' : 'green'} />
            {error ? t('appearance.save_failed') : t('appearance.stored_on_printer')}
          </div>
        }
      >
        {(doc.recovered || doc.unsupported) && (
          <p role="alert" class="mb-3 rounded-md border border-amber/50 bg-amber/10 p-2.5 text-sm text-amber">
            {tpl('appearance.recovered_error', { error: doc.error || t('appearance.recovered_unknown') })}
          </p>
        )}
        <Tabs
          wrap
          items={TABS.map(([Glyph, label, id]) => ({ id, label: t(label), icon: <Glyph size={16} /> }))}
          value={tab}
          onChange={id => openAppearance(id as AppearanceTab)}
        />
        {tab === 'overview' && <Overview />}
        {tab === 'themes' && <Themes />}
        {tab === 'layout' && <LayoutTab />}
        {tab === 'widgets' && <WidgetEditor />}
        {tab === 'touchscreen' && <Touchscreen />}
        {tab === 'profiles' && <Profiles />}
        {tab === 'transfer' && <Transfer />}
        {tab === 'advanced' && <Advanced />}
      </Page>
    </div>
  )
}
