import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tag } from '@/components/ui/badge'
import { Input, Select } from '@/components/ui/field'
import { Notice, Warn } from '@/components/shared'
import { errText, notify, request } from '@/lib/api'
import { experimental, refreshExperiments } from '@/lib/experiments'
import { ask } from '@/lib/confirm'
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
  tiltsBelowTarget,
  turnsConfigured,
} from '@/lib/mesh-align'
import {
  type AlignPreview,
  type AlignSession,
  alignRequest,
  alignRounds,
  alignSession,
  refreshAlign,
  tiltWorst,
} from '@/lib/mesh-align-session'
import { calibrationForm, findMeasure, nozzleName, plates, refreshPlates, tempOk } from '@/lib/plates'
import { usePoll } from '@/lib/poll'
import { NozzleSelect } from '@/pages/bed-plates'
import { SCREW_CORNERS } from '@/lib/screws'
import { openSettings } from '@/lib/state'
import { ls, store } from '@/lib/store'

// Mesh-guided screw alignment (see lib/mesh-align.ts): what the screws have to do, from a plane through a
// whole bed mesh. The analysis only reads meshes. The guided loop below it (lib/mesh-align-session.ts)
// measures, and sends a printer command only after the user has confirmed that very command.

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
  <label class="grid content-between gap-1 text-xs">
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
        <label class="grid content-between gap-1 text-xs">
          {t('bed.align_set_pitch_source')}
          <Input
            type="text"
            maxLength={80}
            value={config.thread_pitch_source}
            onChange={e => saveConfig({ thread_pitch_source: e.currentTarget.value })}
          />
        </label>
        <label class="grid content-between gap-1 text-xs">
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
        <label class="grid content-between gap-1 text-xs">
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

const clock = (seconds: number) => `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`
const CAL_FAILED: Record<string, Key> = {
  homing: 'bed.homing_did_not_finish',
  heating: 'bed.cal_failed_heating',
  busy: 'bed.cal_failed_busy',
  telemetry: 'bed.cal_failed_telemetry',
  probing: 'bed.cal_failed_probing',
}
// What a command of the list does, in the user's language. A command without an entry is shown as it is.
const COMMANDS: [RegExp, Key][] = [
  [/^G28$/, 'bed.align_cmd_home'],
  [/^M140 S0$/, 'bed.align_cmd_bed_off'],
  [/^M140 S/, 'bed.align_cmd_heat'],
  [/^M104 S0$/, 'bed.align_cmd_nozzle_off'],
  [/^BED_MESH_CALIBRATE /, 'bed.align_cmd_probe'],
  [/^BED_MESH_PROFILE LOAD=/, 'bed.align_cmd_profile_load'],
  [/^BED_MESH_PROFILE SAVE=/, 'bed.align_cmd_profile_save'],
  [/^SET_GCODE_OFFSET Z=/, 'bed.align_cmd_offset'],
  [/^TURN_OFF_HEATERS$/, 'bed.align_cmd_heaters_off'],
]
// The confirmation shows every command the request will send, in order, exactly as the backend lists it.
export const commandLines = (preview: AlignPreview, wait?: string) =>
  preview.commands
    .flatMap(command => {
      const known = COMMANDS.find(([pattern]) => pattern.test(command))
      const line = known ? `${command} — ${t(known[1])}` : command
      return wait && /^M140 S[1-9]/.test(command) ? [line, wait] : [line]
    })
    .join('\n')

const ENDED: Record<Exclude<AlignSession['ended'], ''>, Key> = {
  ended: 'bed.align_ended_ended',
  saved: 'bed.align_ended_saved',
  failed: 'bed.align_ended_failed',
  cancelled: 'bed.align_ended_cancelled',
  timeout: 'bed.align_ended_timeout',
  print: 'bed.align_ended_print',
  estop: 'bed.align_ended_estop',
}

// The guided loop: measure, turn the screws as listed, measure again, save the last mesh.
const Guide = ({ config }: { config: AlignConfig }) => {
  const { data: session, supported } = alignSession.use()
  const { rounds } = alignRounds.use()
  const library = plates.use().data
  const form = calibrationForm.use()
  const [busy, setBusy] = useState(false)
  const [plate, setPlate] = useState<string | null>(null)
  const lock = useRef(false)
  if (supported === false || !session) return null
  const active = session.active
  const side: 'A' | 'B' = active ? session.side : form.side === 'default1' ? 'B' : 'A'
  const temp = active ? String(session.temp) : form.temp
  const soak = active ? String(session.soak) : form.soak
  const nozzle = active ? session.nozzle : (form.nozzle ?? library?.nozzle ?? '')
  const candidates = library?.available ? library.plates.filter(p => p.side === side) : []
  const plateId =
    plate !== null && candidates.some(p => p.id === plate)
      ? plate
      : candidates.find(p => p.id === library?.current)?.id || candidates[0]?.id || ''
  const stage = session.calibration.stage
  const probing = stage === 'probing' || stage === 'saving'
  const latest = rounds.find(r => r.round === session.round)
  const before = rounds.find(r => r.round === session.round - 1)
  const done = latest ? tiltsBelowTarget(latest.tilt_x, latest.tilt_y, config.target_tilt_mm) : false
  const spent = active && session.round >= session.rounds

  const run = async (work: () => Promise<void>) => {
    if (lock.current) return
    lock.current = true
    setBusy(true)
    try {
      await work()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      lock.current = false
      setBusy(false)
      void refreshAlign()
    }
  }
  const measure = () =>
    run(async () => {
      if (!tempOk(temp)) return notify(t('common.invalid_bed_temperature'), 'error')
      const lines = [
        side,
        String(Number(temp)),
        String(Number(soak)),
        nozzle,
        String(active ? session.rounds : config.max_rounds),
      ]
      const reply = await alignRequest('measure', lines, preview =>
        ask(
          [
            tpl('bed.align_guide_confirm_measure', { round: Number(preview.round), rounds: Number(preview.rounds) }),
            commandLines(preview, tpl('bed.align_guide_wait', { temp: Number(temp), minutes: Number(preview.soak) })),
            tpl('bed.align_guide_full', { command: 'BED_MESH_CALIBRATE' }),
          ].join('\n\n')
        )
      )
      if (reply && !active) alignRounds.set({ rounds: [] })
    })
  const save = () =>
    run(async () => {
      const target = candidates.find(p => p.id === plateId)
      if (!target) return
      const reply = await alignRequest('save', [target.id], preview =>
        ask(
          [
            tpl('bed.align_guide_confirm_save', { name: target.name, temp: Number(preview.temp) }),
            commandLines(preview),
          ].join('\n\n')
        )
      )
      if (reply?.saved) {
        notify(tpl('bed.measure_saved', { name: target.name, temp: session.temp }))
        void refreshPlates()
      }
    })
  const end = () =>
    run(async () => {
      await alignRequest('end', [], preview =>
        ask(
          [t('bed.align_guide_confirm_end'), commandLines(preview), t('bed.align_guide_after_abort')].join('\n\n'),
          true
        )
      )
    })

  const progress =
    stage === 'homing'
      ? t('bed.cal_homing')
      : stage === 'heating'
        ? tpl('bed.cal_heating', {
            bed: session.calibration.bed === null ? '—' : Math.round(session.calibration.bed),
            temp: session.temp,
          })
        : stage === 'soaking'
          ? tpl('bed.cal_soaking', { temp: session.temp, left: clock(session.calibration.remaining) })
          : tpl('bed.cal_probing', { side: session.side, temp: session.temp })
  const failure = session.calibration.detail || t(CAL_FAILED[session.calibration.error] || 'bed.cal_failed_probing')
  return (
    <div class="mt-3.5 border-t border-edge pt-3" data-align="guide" data-align-active={active ? 'yes' : 'no'}>
      <h3 class="mb-2 text-base font-semibold">{t('bed.align_guide_title')}</h3>
      <p class="mb-3 text-xs text-muted">
        {t('bed.align_guide_intro')}
        <br />
        {tpl('bed.align_guide_full', { command: 'BED_MESH_CALIBRATE' })}
      </p>
      <div class="grid gap-2 text-xs cc2-sm:grid-cols-2 cc2-lg:grid-cols-4">
        <label class="grid gap-1">
          {t('bed.calibration_plate_side')}
          <Select
            value={side}
            disabled={active || busy}
            onChange={e =>
              calibrationForm.set({ side: e.currentTarget.value === 'B' ? 'default1' : 'default', target: null })
            }
          >
            <option value="A">{t('bed.side_a_default')}</option>
            <option value="B">{t('bed.side_b_default1')}</option>
          </Select>
        </label>
        <label class="grid gap-1">
          {t('common.bed_temperature_c')}
          <Input
            type="number"
            min="30"
            max="110"
            step="1"
            value={temp}
            disabled={active || busy}
            onInput={e => calibrationForm.set({ temp: e.currentTarget.value })}
          />
        </label>
        <label class="grid gap-1">
          {t('bed.soak_minutes')}
          <Select
            value={soak}
            disabled={active || busy}
            onChange={e => calibrationForm.set({ soak: e.currentTarget.value })}
          >
            {Array.from({ length: 61 }, (_, minutes) => (
              <option key={minutes} value={String(minutes)}>
                {minutes}
              </option>
            ))}
          </Select>
        </label>
        {library && (
          <label class="grid gap-1">
            {t('common.nozzle')}
            <NozzleSelect
              lib={library}
              value={nozzle}
              disabled={active || busy}
              onChange={id => calibrationForm.set({ nozzle: id })}
            />
          </label>
        )}
      </div>
      {active && (
        <p class="mt-3 text-sm font-semibold" data-align="round">
          {tpl('bed.align_guide_rounds', {
            round: session.measuring ? session.round + 1 : session.round,
            rounds: session.rounds,
          })}
        </p>
      )}
      {rounds.length > 0 && active && (
        <ul class="mt-1 text-xs text-muted" data-align="rounds">
          {rounds.map(r => (
            <li key={r.round}>
              {tpl('bed.align_guide_round_result', { round: r.round, x: signedMm(r.tilt_x), y: signedMm(r.tilt_y) })}
            </li>
          ))}
        </ul>
      )}
      {active && session.measuring && (
        <div data-align="progress" data-align-stage={stage}>
          <Notice icon="bolt">
            <span>
              {progress}
              {probing && <small class="mt-1 block">{t('bed.align_guide_probing')}</small>}
            </span>
          </Notice>
        </div>
      )}
      {active && !session.measuring && latest && (
        <div data-align="advice">
          {before && (
            <p class="mt-2 text-xs text-muted" data-align="trend">
              {t(tiltWorst(latest) < tiltWorst(before) ? 'bed.align_guide_smaller' : 'bed.align_guide_not_smaller')}
            </p>
          )}
          <Notice icon={done ? 'shield' : 'info'}>
            <span>
              {done
                ? t('bed.align_guide_done')
                : spent
                  ? tpl('bed.align_guide_out_of_rounds', { rounds: session.rounds })
                  : t('bed.align_guide_turn')}
            </span>
          </Notice>
        </div>
      )}
      {!active && session.ended && (
        <div data-align="ended" data-align-ended={session.ended}>
          <Notice>
            <span>
              {session.ended === 'failed'
                ? tpl('bed.align_ended_failed', { error: failure })
                : session.ended === 'timeout'
                  ? tpl('bed.align_ended_timeout', { minutes: 20 })
                  : t(ENDED[session.ended])}
              {session.ended !== 'saved' && session.ended !== 'print' && (
                <small class="mt-1 block">{t('bed.align_guide_after_abort')}</small>
              )}
            </span>
          </Notice>
        </div>
      )}
      {session.heaters_off_pending && <p class="mt-2 text-xs text-muted">{t('bed.align_heaters_pending')}</p>}
      <div class="mt-3 flex flex-wrap items-end gap-2">
        <Button
          variant={active && (done || spent) ? 'default' : 'primary'}
          class="min-h-11"
          disabled={busy || session.measuring || spent}
          onClick={() => void measure()}
        >
          {t(active ? 'bed.align_guide_measure_again' : 'bed.align_guide_measure')}
        </Button>
        {active && !session.measuring && session.measured && candidates.length > 0 && (
          <>
            <label class="grid min-w-40 gap-1 text-xs">
              {t('bed.align_guide_save_to')}
              <Select value={plateId} disabled={busy} onChange={e => setPlate(e.currentTarget.value)}>
                {candidates.map(p => (
                  <option key={p.id} value={p.id}>
                    {p.name}
                  </option>
                ))}
              </Select>
            </label>
            <Button
              variant={done || spent ? 'primary' : 'default'}
              class="min-h-11"
              disabled={busy}
              onClick={() => void save()}
            >
              {t('bed.align_guide_save')}
            </Button>
          </>
        )}
        {active && (
          <Button class="min-h-11" disabled={busy || (session.measuring && probing)} onClick={() => void end()}>
            {t('bed.align_guide_end')}
          </Button>
        )}
      </div>
      {active && !session.measuring && session.measured && candidates.length === 0 && (
        <p class="mt-2 text-xs text-muted">{t('bed.align_guide_no_plate')}</p>
      )}
    </div>
  )
}

export const MeshAlign = () => {
  // The settings are an experimental opt-in (Settings → Experimental integrations). Without it the
  // CC2 defaults apply, whatever this browser has stored, and the settings block is not shown.
  const settingsEnabled = Boolean(experimental.use().data?.mesh_align_settings)
  const stored = alignConfig.use()
  const config = settingsEnabled ? stored : ALIGN_DEFAULTS
  const configRef = useRef(config)
  configRef.current = config
  useEffect(() => {
    void refreshExperiments()
  }, [])
  const library = plates.use().data
  const [data, setData] = useState<any>(null)
  const [choice, setChoice] = useState('')
  const [loading, setLoading] = useState(false)
  // The printer's own mesh has been asked for once: only then is a default chosen, so a saved
  // measurement that loaded first is not shown for a moment in place of the active mesh.
  const [answered, setAnswered] = useState(false)
  const alive = useRef(true)
  const load = async (manual = false) => {
    setLoading(true)
    try {
      const reply = await request('/api/mesh')
      if (alive.current) setData(reply)
      return reply
    } catch (e) {
      if (manual) notify(tpl('bed.mesh_unavailable_error', { error: errText(e) }), 'error')
      return null
    } finally {
      if (alive.current) {
        setLoading(false)
        setAnswered(true)
      }
    }
  }
  // The guided loop runs on CC2 Control; the page follows it, faster while a session is active.
  const session = alignSession.use().data
  usePoll(refreshAlign, session?.active ? 2000 : 15000)
  // A completed round: read the mesh it measured, show it and keep its tilt for the comparison.
  const slot = session?.side === 'B' ? 'default1' : 'default'
  useEffect(() => {
    if (!session?.active || !session.measured || session.round < 1) return
    if (alignRounds.get().rounds.some(r => r.round === session.round)) return
    const round = session.round
    void (async () => {
      const reply = await load()
      if (!alive.current || !reply) return
      // The mesh the printer applies now when it is this side's, else the side slot as saved.
      const live = reply?.result?.status?.bed_mesh?.profile_name === slot ? gridFromMeshReply(reply, 'active') : null
      const saved = gridFromMeshReply(reply, slot)
      const grid = live || saved || gridFromMeshReply(reply, 'active')
      const measured = grid ? analyseMesh(grid, configRef.current) : null
      if (!measured) return
      setChoice(live || !saved ? 'active' : slot)
      alignRounds.set(state => ({
        rounds: [
          ...state.rounds.filter(r => r.round < round),
          { round, tilt_x: measured.tilt_x, tilt_y: measured.tilt_y },
        ],
      }))
    })()
  }, [session?.active, session?.round, session?.measured])
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
  const source = sources.find(s => s.id === choice) || (answered ? sources[0] : null) || null
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
      <Guide config={config} />
      {settingsEnabled ? (
        <Settings config={config} />
      ) : (
        <div
          class="mt-3.5 flex flex-wrap items-center gap-2 rounded-md border border-edge px-3 py-2"
          data-align="settings-off"
        >
          <p class="min-w-0 flex-1 text-xs text-muted">{t('bed.align_settings_off')}</p>
          <Button onClick={() => openSettings('integrations')}>{t('bed.align_settings_open')}</Button>
        </div>
      )}
    </Card>
  )
}
