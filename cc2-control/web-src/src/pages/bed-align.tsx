import { useEffect, useId, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { InfoHint } from '@/components/info-hint'
import { Input } from '@/components/ui/field'
import { experimental, refreshExperiments } from '@/lib/experiments'
import { i18n, type Key, t, tpl } from '@/lib/i18n'
import {
  ALIGN_DEFAULTS,
  ALIGN_NOOP_MM,
  PITCH_MAX_MM,
  PITCH_MIN_MM,
  type AlignConfig,
  type AlignDirection,
  type AlignScrewId,
  type AlignShape,
  type ClockTurn,
  clockLabel,
  type MeshAnalysis,
  parseAlignConfig,
  type ScrewCorrection,
} from '@/lib/mesh-align'
import { SCREW_CORNERS } from '@/lib/screws'
import { openSettings } from '@/lib/state'
import { ls, store } from '@/lib/store'

// Parts of the bed alignment with a mesh (see lib/mesh-align.ts): the shape map, the tiles and the experimental
// settings. The analysis only reads meshes; nothing here sends a printer command.

const STORAGE = 'cc2-mesh-align'
export const alignConfig = store<AlignConfig>(parseAlignConfig(ls.get(STORAGE)))
const saveConfig = (patch: Partial<AlignConfig>) => {
  const next = parseAlignConfig(JSON.stringify({ ...alignConfig.get(), ...patch }))
  ls.set(STORAGE, JSON.stringify(next))
  alignConfig.set(next)
}
const resetConfig = () => {
  ls.del(STORAGE)
  alignConfig.set(parseAlignConfig(null))
}

const CORNER: Record<AlignScrewId, Key> = {
  FL: 'bed.corner_front_left',
  FR: 'bed.corner_front_right',
  RR: 'bed.corner_rear_right',
  RL: 'bed.corner_rear_left',
}
export const SHAPE: Record<AlignShape, Key> = {
  flat: 'bed.align_shape_flat',
  dome: 'bed.align_shape_dome',
  bowl: 'bed.align_shape_bowl',
  saddle: 'bed.align_shape_saddle',
  shape: 'bed.align_shape_other',
}

// Millimetres in the UI language's number format, without a sign.
export const mm = (v: number, digits = 3) =>
  new Intl.NumberFormat(i18n.get().lang, { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(
    Math.abs(v)
  )
export const signedMm = (v: number, digits = 3) => {
  const rounded = Number(v.toFixed(digits))
  return `${rounded > 0 ? '+' : rounded < 0 ? '−' : ''}${mm(v, digits)}`
}

export const tiltWords = (value: number, positive: Key, negative: Key) =>
  Math.abs(value) < 0.0005 ? t('bed.align_level') : t(value > 0 ? positive : negative)

// The turn at one screw: the clock-face turn ("CW 01:20") when the pitch and its source are set, the direction
// in words when only the direction is known, nothing otherwise (also under one minute or from 100 turns up).
export const turnLabel = (clock: ClockTurn | null, direction: AlignDirection | null) =>
  clock
    ? clockLabel(clock, direction)
    : direction === null
      ? ''
      : t(direction === 'cw' ? 'bed.align_cw' : 'bed.align_ccw')

// What to do at one screw: millimetres always, the turn only when its sources are set.
export const screwInstruction = (s: ScrewCorrection) => {
  if (s.reference) return t('bed.align_reference')
  if (Math.abs(s.correction) < ALIGN_NOOP_MM) return t('bed.align_noop')
  const text = tpl(s.correction > 0 ? 'bed.align_raise' : 'bed.align_lower', { amount: mm(s.correction, 2) })
  const extra = turnLabel(s.clock, s.direction)
  return extra ? `${text} (${extra})` : text
}

// Same colour scale as the Mesh tab (lib/meshdraw.ts): lowest point blue, then cyan, green, highest yellow.
const STOPS = [
  [24, 123, 213],
  [22, 199, 220],
  [103, 211, 176],
  [250, 203, 69],
]
const heat = (z: number, low: number, high: number) => {
  const n = Math.max(0, Math.min(0.9999, high > low ? (z - low) / (high - low) : 0.5)) * 3,
    k = Math.floor(n),
    f = n - k,
    a = STOPS[k],
    b = STOPS[Math.min(3, k + 1)]
  return `rgb(${a.map((v, i) => Math.round(v + (b[i] - v) * f)).join(',')})`
}

// The plate shape (mesh minus plane) from above, front edge at the bottom, with the screws.
export const ShapeMap = ({
  analysis,
  config,
  reference,
  active,
  onPick,
}: {
  // The corner picked in the table (its mark is highlighted); a click on a mark picks its corner (display only).
  active?: string | null
  onPick?: (id: 'FL' | 'FR' | 'RR' | 'RL') => void
  analysis: MeshAnalysis
  config: AlignConfig
  // The plate shape at the reference screw (as in the table's plate-shape column), to read a cell against it.
  reference?: { n: number; z: number }
}) => {
  const [picked, setPicked] = useState<{ x: number; y: number; z: number } | null>(null)
  const points = analysis.residual
  const unique = (values: number[]) => [...new Set(values.map(v => Number(v.toFixed(4))))].sort((a, b) => a - b)
  const xs = unique(points.map(p => p.x)),
    ys = unique(points.map(p => p.y))
  const stepX = xs.length > 1 ? (xs[xs.length - 1] - xs[0]) / (xs.length - 1) : 10,
    stepY = ys.length > 1 ? (ys[ys.length - 1] - ys[0]) / (ys.length - 1) : 10
  const left = Math.min(xs[0] - stepX / 2, ...config.screws.map(s => s.x - 12)),
    right = Math.max(xs[xs.length - 1] + stepX / 2, ...config.screws.map(s => s.x + 12)),
    bottom = Math.min(ys[0] - stepY / 2, ...config.screws.map(s => s.y - 12)),
    top = Math.max(ys[ys.length - 1] + stepY / 2, ...config.screws.map(s => s.y + 12))
  const zs = points.map(p => p.z),
    low = Math.min(...zs),
    high = Math.max(...zs),
    // The colours stretch over this mesh's own deviation, symmetric around the plane (0 = middle colour).
    peak = Math.max(0.005, Math.abs(low), Math.abs(high))
  return (
    <div data-align="heatmap">
      <svg
        role="img"
        aria-label={tpl('bed.align_heatmap_label', { min: signedMm(low), max: signedMm(high) })}
        viewBox={`${left} 0 ${right - left} ${top - bottom}`}
        class="block aspect-square w-full rounded-lg bg-well"
      >
        {points.map(p => (
          // biome-ignore lint/a11y/useSemanticElements: an SVG group cannot be a <button>
          <g
            key={`${p.x},${p.y}`}
            role="button"
            tabindex={0}
            class="cursor-pointer"
            aria-label={`X${Number(p.x.toFixed(1))} Y${Number(p.y.toFixed(1))}: ${signedMm(p.z)} mm`}
            onClick={() => setPicked(p)}
            onKeyDown={e => {
              if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault()
                setPicked(p)
              }
            }}
          >
            <rect
              x={p.x - stepX / 2}
              y={top - p.y - stepY / 2}
              width={stepX + 0.3}
              height={stepY + 0.3}
              fill={heat(p.z, -peak, peak)}
              stroke="var(--well)"
              stroke-width="0.8"
            />
            <title>{`X${Number(p.x.toFixed(1))} Y${Number(p.y.toFixed(1))}: ${signedMm(p.z)} mm`}</title>
          </g>
        ))}
        {points.map(p => (
          <text
            key={`v${p.x},${p.y}`}
            x={p.x}
            y={top - p.y + 2.6}
            text-anchor="middle"
            font-size={Math.min(8, stepX / 3)}
            fill={p.z < -peak / 3 ? '#ffffff' : '#0b1d26'}
            pointer-events="none"
          >
            {Math.round(p.z * 1000)}
          </text>
        ))}
        {config.screws.map(s => {
          const corner = SCREW_CORNERS.find(c => c.id === s.id)
          const on = active === s.id
          const pick = () => corner && onPick?.(corner.id)
          return (
            // biome-ignore lint/a11y/useSemanticElements: an SVG group cannot be a <button>
            <g
              key={s.id}
              role="button"
              tabindex={0}
              aria-pressed={on}
              aria-label={corner?.n}
              class="cursor-pointer focus-visible:outline-2 focus-visible:outline-cyan"
              data-screw-pick-map={s.id}
              onClick={pick}
              onKeyDown={e => {
                if (e.key === 'Enter' || e.key === ' ') {
                  e.preventDefault()
                  pick()
                }
              }}
            >
              <circle
                cx={s.x}
                cy={top - s.y}
                r="11"
                fill={on ? 'var(--cyan)' : 'var(--panel)'}
                fill-opacity={on ? '1' : '0.87'}
                stroke={on ? 'var(--text)' : 'var(--muted)'}
                stroke-width={on ? '1.8' : '0.9'}
              />
              <text
                x={s.x}
                y={top - s.y + 3.8}
                text-anchor="middle"
                font-size="10.5"
                font-weight="500"
                fill={on ? 'var(--ink)' : 'var(--text)'}
                pointer-events="none"
              >
                {corner?.n}
              </text>
            </g>
          )
        })}
      </svg>
      <div class="mt-1.5 flex items-center gap-2 text-xs text-muted">
        <span>{signedMm(-peak)} mm</span>
        <i
          class="h-2 flex-1 rounded-full"
          style={{ background: `linear-gradient(90deg, ${STOPS.map(c => `rgb(${c.join(',')})`).join(', ')})` }}
        />
        <span>{signedMm(peak)} mm</span>
      </div>
      {picked ? (
        <dl class="mx-auto mt-1.5 grid w-fit grid-cols-[auto_auto] gap-x-3 gap-y-0.5 text-xs" data-align="picked">
          <dt class="text-muted">{t('bed.align_pick_pos')}</dt>
          <dd class="text-right tabular-nums">{`X${Number(picked.x.toFixed(1))} Y${Number(picked.y.toFixed(1))}`}</dd>
          <dt class="text-muted">{t('bed.align_pick_plane')}</dt>
          <dd class="text-right tabular-nums">{signedMm(picked.z)} mm</dd>
          {reference && (
            <>
              <dt class="text-muted">{t('bed.align_pick_ref')}</dt>
              <dd class="text-right tabular-nums">{signedMm(picked.z - reference.z)} mm</dd>
            </>
          )}
        </dl>
      ) : (
        <p class="mt-1 text-center text-xs text-muted" data-align="picked">
          {t('bed.align_map_range')}
        </p>
      )}
      <div class="mt-1 text-center text-xs text-muted">{t('bed.top_view_front_edge_at_bottom')}</div>
    </div>
  )
}

export const Tile = ({ label, value, sub, name }: { label: string; value: string; sub: string; name: string }) => (
  <div class="rounded-lg border border-edge bg-field/60 p-2.5" data-align={name}>
    <span class="text-xs">{label}</span>
    <strong class="mt-1 block text-xl font-semibold whitespace-nowrap">{value}</strong>
    <p class="mt-1 text-xs text-muted">{sub}</p>
  </div>
)

const NumberField = ({
  label,
  hint,
  value,
  step,
  min,
  max,
  onCommit,
}: {
  label: string
  // Where the value takes effect, behind an (i) next to the name.
  hint?: string
  value: number | null
  step: string
  min: string
  max: string
  onCommit: (value: number | null) => void
}) => {
  // The (i) stands beside the name, outside the label: the field keeps its plain name for screen readers.
  const id = useId()
  return (
    <div class="grid content-between gap-1 text-xs">
      <span class="flex min-h-5 items-center gap-1">
        <label for={id}>{label}</label>
        {hint && <InfoHint text={hint} subtle data-align-hint />}
      </span>
      <Input
        id={id}
        type="number"
        step={step}
        min={min}
        max={max}
        value={value ?? ''}
        onChange={e => {
          const text = e.currentTarget.value.trim()
          onCommit(text === '' || !Number.isFinite(Number(text)) ? null : Number(text))
        }}
      />
    </div>
  )
}

const Settings = ({ config }: { config: AlignConfig }) => {
  const number = (key: keyof AlignConfig) => (value: number | null) =>
    saveConfig({ [key]: value ?? ALIGN_DEFAULTS[key] } as Partial<AlignConfig>)
  return (
    <details class="mt-3.5 rounded-md border border-edge px-3" data-align="settings">
      <summary class="flex min-h-11 cursor-pointer items-center text-base font-semibold">
        {t('bed.align_settings')}
      </summary>
      <p class="mb-3 text-xs text-muted">{t('bed.align_settings_note')}</p>
      <div class="grid gap-3 cc2-sm:grid-cols-2 cc2-lg:grid-cols-4">
        <NumberField
          label={t('bed.align_set_span')}
          hint={t('bed.align_hint_span')}
          value={config.tilt_span_mm}
          step="1"
          min="10"
          max="1000"
          onCommit={number('tilt_span_mm')}
        />
        <NumberField
          label={t('bed.align_set_warn')}
          hint={t('bed.align_hint_warn')}
          value={config.shape_warn_mm}
          step="0.01"
          min="0.01"
          max="5"
          onCommit={number('shape_warn_mm')}
        />
        <NumberField
          label={t('bed.align_set_flat')}
          hint={t('bed.align_hint_flat')}
          value={config.flat_range_mm}
          step="0.01"
          min="0"
          max="5"
          onCommit={number('flat_range_mm')}
        />
        {/* The pitch of the levelling screw: the CC2 default, or the value the user sets here. */}
        <div data-align="pitch">
          <NumberField
            label={t('bed.align_set_pitch')}
            hint={t('bed.align_hint_pitch')}
            value={config.thread_pitch_user_mm ?? ALIGN_DEFAULTS.thread_pitch_mm}
            step="0.01"
            min={String(PITCH_MIN_MM)}
            max={String(PITCH_MAX_MM)}
            onCommit={value =>
              saveConfig({
                thread_pitch_user_mm:
                  value == null || value === ALIGN_DEFAULTS.thread_pitch_mm ? null : (value as number),
              })
            }
          />
        </div>
      </div>
      <h3 class="mt-3.5 mb-2 flex items-center gap-1.5 text-base font-semibold">
        {t('bed.align_set_screws')}
        <InfoHint text={t('bed.align_hint_screws')} data-align-hint />
      </h3>
      <div class="grid gap-3 cc2-sm:grid-cols-2 cc2-lg:grid-cols-4">
        {config.screws.map(s => (
          <div key={s.id} class="grid grid-cols-2 gap-2">
            {(['x', 'y'] as const).map(axis => (
              <NumberField
                key={axis}
                label={`${t(CORNER[s.id])} ${axis.toUpperCase()}`}
                value={s[axis]}
                step="1"
                min="0"
                max="400"
                onCommit={value =>
                  saveConfig({
                    screws: config.screws.map(o =>
                      o.id === s.id
                        ? { ...o, [axis]: value ?? ALIGN_DEFAULTS.screws.find(d => d.id === s.id)?.[axis] ?? 0 }
                        : o
                    ),
                  })
                }
              />
            ))}
          </div>
        ))}
      </div>
      <div class="mt-3.5 flex min-h-11 items-center gap-2 text-xs">
        <label class="flex items-center gap-2">
          <input
            type="checkbox"
            class="size-4 shrink-0 accent-cyan"
            checked={config.row0_is_min_y}
            onChange={e => saveConfig({ row0_is_min_y: e.currentTarget.checked })}
          />
          {t('bed.align_set_row0')}
        </label>
        <InfoHint text={t('bed.align_hint_row0')} subtle data-align-hint />
      </div>
      <div class="flex flex-wrap gap-2 border-t border-edge py-3">
        <Button class="min-h-11" onClick={resetConfig}>
          {t('bed.align_reset')}
        </Button>
      </div>
    </details>
  )
}

export const useAlignConfig = () => {
  // The settings are an experimental opt-in (Settings → Experimental integrations). Without it the
  // CC2 defaults apply, whatever this browser has stored, and the settings block is not shown.
  const settingsEnabled = Boolean(experimental.use().data?.mesh_align_settings)
  const stored = alignConfig.use()
  useEffect(() => {
    void refreshExperiments()
  }, [])
  return { config: settingsEnabled ? stored : ALIGN_DEFAULTS, settingsEnabled }
}

// The experimental alignment settings, or the note that the CC2 defaults apply.
export const AlignSettingsBlock = ({ config, settingsEnabled }: { config: AlignConfig; settingsEnabled: boolean }) =>
  settingsEnabled ? (
    <Settings config={config} />
  ) : (
    <div
      class="mt-3.5 flex flex-wrap items-center gap-2 rounded-md border border-edge px-3 py-2"
      data-align="settings-off"
    >
      <p class="min-w-[min(100%,14rem)] flex-1 text-xs text-muted">{t('bed.align_settings_off')}</p>
      <Button onClick={() => openSettings('integrations')}>{t('bed.align_settings_open')}</Button>
    </div>
  )
