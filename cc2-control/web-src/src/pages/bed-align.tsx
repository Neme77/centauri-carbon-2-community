import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Notice, Warn } from '@/components/shared'
import { errText, notify, request } from '@/lib/api'
import { i18n, type Key, t, tpl } from '@/lib/i18n'
import {
  ALIGN_DEFAULTS,
  ALIGN_NOOP_MM,
  type AlignConfig,
  type AlignScrewId,
  type AlignShape,
  analyseMesh,
  directionConfigured,
  gridFromMeshReply,
  type MeshAnalysis,
  type MeshGrid,
  parseAlignConfig,
  type ScrewCorrection,
  turnsConfigured,
} from '@/lib/mesh-align'
import { findMeasure, nozzleName, plates, refreshPlates } from '@/lib/plates'
import { SCREW_CORNERS } from '@/lib/screws'
import { ls, store } from '@/lib/store'

// Mesh-guided screw alignment (see lib/mesh-align.ts): what the screws have to do, from a plane through a
// whole bed mesh. This card only reads meshes; it sends nothing to the printer.

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

// The Elegoo wiki's sentence, used only when the user selects it as the source: turning the locknut
// clockwise increases the distance between nozzle and bed, which lowers the bed at that corner.
const WIKI_SOURCE = 'Elegoo wiki'

const CORNER: Record<AlignScrewId, Key> = {
  FL: 'bed.corner_front_left',
  FR: 'bed.corner_front_right',
  RR: 'bed.corner_rear_right',
  RL: 'bed.corner_rear_left',
}
const SHAPE: Record<AlignShape, Key> = {
  flat: 'bed.align_shape_flat',
  dome: 'bed.align_shape_dome',
  bowl: 'bed.align_shape_bowl',
  saddle: 'bed.align_shape_saddle',
  shape: 'bed.align_shape_other',
}
const PROFILES: [string, Key][] = [
  ['default', 'bed.side_a_default'],
  ['default1', 'bed.side_b_default1'],
]

// Millimetres in the UI language's number format, without a sign.
const mm = (v: number, digits = 3) =>
  new Intl.NumberFormat(i18n.get().lang, { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(
    Math.abs(v)
  )
const signedMm = (v: number, digits = 3) => {
  const rounded = Number(v.toFixed(digits))
  return `${rounded > 0 ? '+' : rounded < 0 ? '−' : ''}${mm(v, digits)}`
}

const tiltWords = (value: number, positive: Key, negative: Key) =>
  Math.abs(value) < 0.0005 ? t('bed.align_level') : t(value > 0 ? positive : negative)

// What to do at one screw: millimetres always, turns and direction only when their sources are set.
export const screwInstruction = (s: ScrewCorrection) => {
  if (s.reference) return t('bed.align_reference')
  if (Math.abs(s.correction) < ALIGN_NOOP_MM) return t('bed.align_noop')
  const text = tpl(s.correction > 0 ? 'bed.align_raise' : 'bed.align_lower', { amount: mm(s.correction, 2) })
  const extra = [
    s.turns === null ? '' : tpl('bed.align_turns', { turns: mm(s.turns, 2) }),
    s.direction === null ? '' : t(s.direction === 'cw' ? 'bed.align_cw' : 'bed.align_ccw'),
  ].filter(Boolean)
  return extra.length ? `${text} (${extra.join(', ')})` : text
}

// Low is blue, the plane is light grey, high is red: the same scale on both sides of zero.
const heat = (z: number, peak: number) => {
  const k = peak > 0 ? Math.max(-1, Math.min(1, z / peak)) : 0
  const from = [226, 232, 240],
    to = k < 0 ? [37, 99, 235] : [220, 38, 38]
  return `rgb(${from.map((v, i) => Math.round(v + (to[i] - v) * Math.abs(k))).join(',')})`
}

// The plate shape (mesh minus plane) from above, front edge at the bottom, with the screws.
const ShapeMap = ({ analysis, config }: { analysis: MeshAnalysis; config: AlignConfig }) => {
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
    peak = Math.max(Math.abs(low), Math.abs(high))
  return (
    <div data-align="heatmap">
      <svg
        role="img"
        aria-label={tpl('bed.align_heatmap_label', { min: signedMm(low), max: signedMm(high) })}
        viewBox={`${left} 0 ${right - left} ${top - bottom}`}
        class="block aspect-square w-full rounded-lg bg-well"
      >
        {points.map(p => (
          <rect
            key={`${p.x},${p.y}`}
            x={p.x - stepX / 2}
            y={top - p.y - stepY / 2}
            width={stepX + 0.3}
            height={stepY + 0.3}
            fill={heat(p.z, peak)}
          >
            <title>{`X${Number(p.x.toFixed(1))} Y${Number(p.y.toFixed(1))}: ${signedMm(p.z)} mm`}</title>
          </rect>
        ))}
        {config.screws.map(s => (
          <g key={s.id}>
            <circle cx={s.x} cy={top - s.y} r="9" fill="#0f172a" stroke="#f8fafc" stroke-width="1.5" />
            <text x={s.x} y={top - s.y + 4} text-anchor="middle" font-size="11" fill="#f8fafc">
              {SCREW_CORNERS.find(c => c.id === s.id)?.n}
            </text>
          </g>
        ))}
      </svg>
      <div class="mt-1.5 flex items-center gap-2 text-xs text-muted">
        <span>{signedMm(low)} mm</span>
        <i
          class="h-2 flex-1 rounded-full"
          style={{ background: `linear-gradient(90deg, ${heat(-1, 1)}, ${heat(0, 1)}, ${heat(1, 1)})` }}
        />
        <span>{signedMm(high)} mm</span>
      </div>
      <div class="mt-1 text-center text-xs text-muted">{t('bed.top_view_front_edge_at_bottom')}</div>
    </div>
  )
}

const Tile = ({ label, value, sub, name }: { label: string; value: string; sub: string; name: string }) => (
  <div class="rounded-lg border border-edge bg-field/60 p-2.5" data-align={name}>
    <span class="text-xs">{label}</span>
    <strong class="mt-1 block text-xl font-semibold whitespace-nowrap">{value}</strong>
    <p class="mt-1 text-xs text-muted">{sub}</p>
  </div>
)

const NumberField = ({
  label,
  value,
  step,
  min,
  max,
  onCommit,
}: {
  label: string
  value: number | null
  step: string
  min: string
  max: string
  onCommit: (value: number | null) => void
}) => (
  <label class="grid gap-1 text-xs">
    {label}
    <Input
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
  </label>
)

const Settings = ({ config }: { config: AlignConfig }) => {
  const number = (key: keyof AlignConfig) => (value: number | null) =>
    saveConfig({ [key]: value ?? ALIGN_DEFAULTS[key] } as Partial<AlignConfig>)
  const rotation = config.clockwise_raises_bed === null ? '' : config.clockwise_raises_bed ? 'raises' : 'lowers'
  return (
    <details class="mt-3.5 rounded-md border border-edge px-3" data-align="settings">
      <summary class="flex min-h-11 cursor-pointer items-center text-base font-semibold">
        {t('bed.align_settings')}
      </summary>
      <p class="mb-3 text-xs text-muted">{t('bed.align_settings_note')}</p>
      <div class="grid gap-3 cc2-sm:grid-cols-2 cc2-lg:grid-cols-4">
        <NumberField
          label={t('bed.align_set_span')}
          value={config.tilt_span_mm}
          step="1"
          min="10"
          max="1000"
          onCommit={number('tilt_span_mm')}
        />
        <NumberField
          label={t('bed.align_set_target')}
          value={config.target_tilt_mm}
          step="0.005"
          min="0.005"
          max="2"
          onCommit={number('target_tilt_mm')}
        />
        <NumberField
          label={t('bed.align_set_rounds')}
          value={config.max_rounds}
          step="1"
          min="1"
          max="10"
          onCommit={number('max_rounds')}
        />
        <NumberField
          label={t('bed.align_set_warn')}
          value={config.shape_warn_mm}
          step="0.01"
          min="0.01"
          max="5"
          onCommit={number('shape_warn_mm')}
        />
        <NumberField
          label={t('bed.align_set_flat')}
          value={config.flat_range_mm}
          step="0.01"
          min="0"
          max="5"
          onCommit={number('flat_range_mm')}
        />
        <NumberField
          label={t('bed.align_set_pitch')}
          value={config.thread_pitch_mm}
          step="0.05"
          min="0.05"
          max="5"
          onCommit={value => saveConfig({ thread_pitch_mm: value })}
        />
        <label class="grid gap-1 text-xs">
          {t('bed.align_set_pitch_source')}
          <Input
            type="text"
            maxLength={80}
            value={config.thread_pitch_source}
            onChange={e => saveConfig({ thread_pitch_source: e.currentTarget.value })}
          />
        </label>
        <label class="grid gap-1 text-xs">
          {t('bed.align_set_rotation')}
          <Select
            value={rotation}
            onChange={e => {
              const value = e.currentTarget.value
              saveConfig({ clockwise_raises_bed: value === '' ? null : value === 'raises' })
            }}
          >
            <option value="">{t('bed.align_rotation_unset')}</option>
            <option value="raises">{t('bed.align_rotation_cw_raises')}</option>
            <option value="lowers">{t('bed.align_rotation_cw_lowers')}</option>
          </Select>
        </label>
        <label class="grid gap-1 text-xs">
          {t('bed.align_set_rotation_source')}
          <Input
            type="text"
            maxLength={80}
            value={config.rotation_source}
            onChange={e => saveConfig({ rotation_source: e.currentTarget.value })}
          />
        </label>
      </div>
      <p class="mt-3 text-xs text-muted">{t('bed.align_wiki_note')}</p>
      <div class="mt-2 flex flex-wrap gap-2">
        <Button
          class="min-h-11"
          onClick={() => saveConfig({ clockwise_raises_bed: false, rotation_source: WIKI_SOURCE })}
        >
          {t('bed.align_wiki_use')}
        </Button>
      </div>
      <h3 class="mt-3.5 mb-2 text-base font-semibold">{t('bed.align_set_screws')}</h3>
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
      <label class="mt-3 flex min-h-11 items-center gap-2 text-xs">
        <input
          type="checkbox"
          class="size-4 shrink-0 accent-cyan"
          checked={config.row0_is_min_y}
          onChange={e => saveConfig({ row0_is_min_y: e.currentTarget.checked })}
        />
        {t('bed.align_set_row0')}
      </label>
      <div class="flex flex-wrap gap-2 border-t border-edge py-3">
        <Button class="min-h-11" onClick={resetConfig}>
          {t('bed.align_reset')}
        </Button>
      </div>
    </details>
  )
}

export const MeshAlign = () => {
  const config = alignConfig.use()
  const library = plates.use().data
  const [data, setData] = useState<any>(null)
  const [choice, setChoice] = useState('')
  const [loading, setLoading] = useState(false)
  const alive = useRef(true)
  const load = async (manual = false) => {
    setLoading(true)
    try {
      const reply = await request('/api/mesh')
      if (alive.current) setData(reply)
    } catch (e) {
      if (manual) notify(tpl('bed.mesh_unavailable_error', { error: errText(e) }), 'error')
    } finally {
      if (alive.current) setLoading(false)
    }
  }
  useEffect(() => {
    alive.current = true
    void load()
    void refreshPlates() // saved measurements are offered next to the printer's meshes
    return () => {
      alive.current = false
    }
  }, [])

  const sources = useMemo(() => {
    const out: { id: string; label: string; grid: MeshGrid; group?: string }[] = []
    const active = gridFromMeshReply(data, 'active')
    if (active) out.push({ id: 'active', label: t('bed.active_mesh'), grid: active })
    for (const [name, label] of PROFILES) {
      const grid = gridFromMeshReply(data, name)
      if (grid) out.push({ id: name, label: t(label), grid })
    }
    for (const p of library?.plates || [])
      for (const m of p.measures)
        out.push({
          id: `measure:${m.id}`,
          label: [`${m.temp} °C`, nozzleName(library, m.nozzle)].filter(Boolean).join(' · '),
          grid: m.mesh,
          group: tpl('plates.plate_option', { name: p.name }),
        })
    return out
  }, [data, library])
  // The mesh the printer applies now is analysed unless another one is chosen.
  const source = sources.find(s => s.id === choice) || sources[0] || null
  const analysis = useMemo(() => (source ? analyseMesh(source.grid, config) : null), [source, config])
  const measured = source?.id.startsWith('measure:') ? findMeasure(library, source.id.slice(8)) : null
  const groups = [...new Set(sources.map(s => s.group).filter(Boolean))] as string[]

  return (
    <Card id="meshAlign" class="mb-3.5">
      <CardHead
        icon="grid"
        title="bed.align_title"
        end={
          <Tag tone={analysis?.aligned ? 'ok' : 'warning'} data-align="tag">
            {t(analysis ? (analysis.aligned ? 'bed.align_tag_done' : 'bed.align_tag_open') : 'bed.no_measurement')}
          </Tag>
        }
      />
      <p class="mb-3 text-xs text-muted">{t('bed.align_intro')}</p>
      <div class="flex flex-wrap items-end gap-2">
        <label class="grid min-w-56 flex-1 gap-1 text-xs">
          {t('bed.align_mesh_source')}
          <Select
            value={source?.id || ''}
            disabled={!sources.length}
            onChange={e => setChoice(e.currentTarget.value)}
            data-align="source"
          >
            {!sources.length && <option value="">{t('bed.align_no_mesh_option')}</option>}
            {sources
              .filter(s => !s.group)
              .map(s => (
                <option key={s.id} value={s.id}>
                  {s.label}
                </option>
              ))}
            {groups.map(group => (
              <optgroup key={group} label={group}>
                {sources
                  .filter(s => s.group === group)
                  .map(s => (
                    <option key={s.id} value={s.id}>
                      {s.label}
                    </option>
                  ))}
              </optgroup>
            ))}
          </Select>
        </label>
        <Button class="min-h-9" disabled={loading} onClick={() => void load(true)}>
          {t('bed.align_reload')}
        </Button>
      </div>
      {!analysis ? (
        <Notice>{t('bed.align_no_mesh')}</Notice>
      ) : (
        <>
          <p class="mt-2 text-xs text-muted" data-align="basis">
            {tpl('bed.align_basis', { n: analysis.count })}
            {measured ? ` · ${measured.plate.name}` : ''}
          </p>
          <h3 class="mt-3.5 mb-2 text-base font-semibold">{t('bed.align_tilt_title')}</h3>
          <div class="grid grid-cols-1 gap-2.5 cc2-sm:grid-cols-2">
            <Tile
              name="tilt-x"
              label={t('bed.align_tilt_x')}
              value={`${signedMm(analysis.tilt_x)} mm`}
              sub={`${tiltWords(analysis.tilt_x, 'bed.align_right_high', 'bed.align_left_high')} · ${tpl('bed.align_over_span', { span: analysis.span })}`}
            />
            <Tile
              name="tilt-y"
              label={t('bed.align_tilt_y')}
              value={`${signedMm(analysis.tilt_y)} mm`}
              sub={`${tiltWords(analysis.tilt_y, 'bed.align_back_high', 'bed.align_front_high')} · ${tpl('bed.align_over_span', { span: analysis.span })}`}
            />
          </div>
          <p class="mt-2 text-xs text-muted">{tpl('bed.align_target', { target: mm(config.target_tilt_mm) })}</p>

          <h3 class="mt-3.5 mb-2 text-base font-semibold">{t('bed.align_screws_title')}</h3>
          <p class="mb-2 text-xs text-muted">{tpl('bed.align_screws_note', { span: analysis.span })}</p>
          <ul class="overflow-hidden rounded-md border border-edge text-xs" data-align="screws">
            {analysis.screws.map((s, i) => (
              <li
                key={s.id}
                data-align-screw={s.id}
                class={`flex flex-wrap items-center gap-x-3 gap-y-1 p-2 ${i ? 'border-t border-edge' : ''}`}
              >
                <b class="grid size-6.5 shrink-0 place-items-center rounded-full border border-muted font-medium">
                  {SCREW_CORNERS.find(c => c.id === s.id)?.n}
                </b>
                <span class="min-w-28 font-semibold">
                  {t(CORNER[s.id])}
                  <small class="block font-normal text-muted">
                    X{s.x} Y{s.y}
                  </small>
                </span>
                <span class="flex-1 break-words">{screwInstruction(s)}</span>
              </li>
            ))}
          </ul>
          {!turnsConfigured(config) && <p class="mt-2 text-xs text-muted">{t('bed.align_no_turns')}</p>}
          {!directionConfigured(config) && <p class="mt-1 text-xs text-muted">{t('bed.align_no_direction')}</p>}
          {!analysis.covers_screws && <Warn>{t('bed.align_extrapolated')}</Warn>}

          <h3 class="mt-3.5 mb-2 text-base font-semibold">{t('bed.align_shape_title')}</h3>
          <div class="grid gap-3 cc2-sm:grid-cols-[minmax(0,18rem)_1fr]">
            <ShapeMap analysis={analysis} config={config} />
            <div class="grid content-start gap-2.5">
              <Tile
                name="shape"
                label={t('bed.align_shape_label')}
                value={t(SHAPE[analysis.shape])}
                sub={tpl('bed.align_shape_range', {
                  range: mm(analysis.residual_range),
                  rms: mm(analysis.residual_rms),
                })}
              />
              <p class="text-xs text-muted">{tpl('bed.align_raw_range', { range: mm(analysis.raw_range) })}</p>
            </div>
          </div>

          <div data-align="verdict" data-aligned={analysis.aligned ? 'yes' : 'no'}>
            <Notice icon={analysis.aligned ? 'shield' : 'info'}>
              <span>
                {analysis.aligned
                  ? tpl('bed.align_verdict_done', {
                      shape: t(SHAPE[analysis.shape]),
                      range: mm(analysis.residual_range),
                    })
                  : tpl('bed.align_verdict_open', {
                      x: signedMm(analysis.tilt_x),
                      y: signedMm(analysis.tilt_y),
                      target: mm(config.target_tilt_mm),
                    })}
              </span>
            </Notice>
          </div>
          {analysis.shape_warning && (
            <div data-align="shape-warning">
              <Warn>
                {tpl('bed.align_shape_warning', {
                  range: mm(analysis.residual_range),
                  limit: mm(config.shape_warn_mm, 2),
                })}
              </Warn>
            </div>
          )}
        </>
      )}
      <Settings config={config} />
    </Card>
  )
}
