import { useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input, PasswordInput } from '@/components/ui/field'
import { Notice, Warn } from '@/components/shared'
import { errText, notify, post, request } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { duration } from '@/lib/format'
import { t, tpl } from '@/lib/i18n'
import { lanRequest, useLanCode } from '@/lib/lan-code'
import { usePoll } from '@/lib/poll'

// GET /api/nozzle-expansion: the firmware's [z_compensation] table (vendor configuration and the
// one saved in autosave.cfg), the calibration CC2 Control runs with NOZZLE_TEMP_CALIBRATE and its result.
type Table = { start: number; diff: number; values: number[] }
type Expansion = {
  available: boolean
  editable: boolean
  state: 'idle' | 'running' | 'cooling' | 'saving' | 'recovery'
  result: '' | 'saved' | 'rejected' | 'failed' | 'interrupted'
  detail: string
  start: number
  end: number
  step: number
  delay: number
  samples: number
  elapsed: number
  nozzle: number | null
  target: number | null
  reference: number
  factory: Table | null
  override: Table | null
  measured: Table | null
  slope_um: number
  residual_um: number
  pending_restart: boolean
  restart: string
}

const STAGES = { cooling: 'nozzle.cooling', saving: 'nozzle.saving', recovery: 'nozzle.recovery' } as const
const RESULTS = { rejected: 'nozzle.rejected', failed: 'nozzle.failed', interrupted: 'nozzle.interrupted' } as const
const SECONDS_PER_STEP = 35 // heating 5-10 C, the probes and the lift, on top of the hold
const SETUP_SECONDS = 150 // homing and the nozzle clean before the first step

const temps = (table: Table) => table.values.map((_, i) => table.start + i * table.diff)
// The table's value at `temp`, interpolated, or extrapolated from the nearest two points.
const at = (table: Table, temp: number) => {
  const v = table.values,
    i = Math.min(Math.max(Math.floor((temp - table.start) / table.diff), 0), v.length - 2)
  return v[i] + ((temp - (table.start + i * table.diff)) / table.diff) * (v[i + 1] - v[i])
}
const slope = (table: Table) => {
  const x = temps(table),
    n = x.length,
    mx = x.reduce((a, b) => a + b, 0) / n,
    my = table.values.reduce((a, b) => a + b, 0) / n
  let sxx = 0,
    sxy = 0
  x.forEach((xi, i) => {
    sxx += (xi - mx) ** 2
    sxy += (xi - mx) * (table.values[i] - my)
  })
  return (sxy / sxx) * 1000
}

// Growth since the reference temperature (what the printer applies), one line per table.
const Chart = ({
  lines,
  reference,
}: {
  lines: { table: Table; label: string; class: string }[]
  reference: number
}) => {
  const W = 560,
    H = 200,
    pad = { l: 44, r: 30, t: 10, b: 26 }
  const pts = lines.map(l => temps(l.table).map((x, i) => [x, (l.table.values[i] - at(l.table, reference)) * 1000]))
  const all = pts.flat()
  const x0 = Math.min(...all.map(p => p[0])),
    x1 = Math.max(...all.map(p => p[0]))
  const y0 = Math.min(0, ...all.map(p => p[1])),
    y1 = Math.max(10, ...all.map(p => p[1]))
  const sx = (x: number) => pad.l + ((x - x0) / (x1 - x0 || 1)) * (W - pad.l - pad.r)
  const sy = (y: number) => H - pad.b - ((y - y0) / (y1 - y0 || 1)) * (H - pad.t - pad.b)
  const ticks = [y0, (y0 + y1) / 2, y1]
  return (
    <figure class="mt-3">
      <svg viewBox={`0 0 ${W} ${H}`} class="w-full" role="img" aria-label={t('nozzle.chart')}>
        {ticks.map(y => (
          <g key={y}>
            <line x1={pad.l} x2={W - pad.r} y1={sy(y)} y2={sy(y)} class="stroke-current text-edge" stroke-width="1" />
            <text x={pad.l - 6} y={sy(y) + 4} text-anchor="end" class="fill-current text-muted" font-size="11">
              {Math.round(y)}
            </text>
          </g>
        ))}
        {[x0, reference, x1].map(x => (
          <text key={x} x={sx(x)} y={H - 8} text-anchor="middle" class="fill-current text-muted" font-size="11">
            {Math.round(x)} °C
          </text>
        ))}
        {pts.map((p, i) => (
          <polyline
            key={lines[i].label}
            points={p.map(([x, y]) => `${sx(x)},${sy(y)}`).join(' ')}
            fill="none"
            stroke-width="2"
            class={`stroke-current ${lines[i].class}`}
          />
        ))}
      </svg>
      <figcaption class="mt-1 flex flex-wrap gap-3 text-xs text-muted">
        <span>{t('nozzle.chart')} (µm)</span>
        {lines.map(l => (
          <span key={l.label} class={l.class}>
            ━ {l.label}
          </span>
        ))}
      </figcaption>
    </figure>
  )
}

export const NozzleExpansion = () => {
  const [state, setState] = useState<Expansion | null>(null)
  const [start, setStart] = useState('130'),
    [end, setEnd] = useState('330'), // 10 C past 320 keeps prints up to 320 C inside the table
    [step, setStep] = useState('10')
  const [checks, setChecks] = useState([false, false, false])
  const [code, setCode] = useLanCode()
  const [busy, setBusy] = useState(false)
  const [refused, setRefused] = useState('') // a refused start stays next to the button, not only in a toast
  const inFlight = useRef(false)
  const [stale, setStale] = useState(false) // a failed poll keeps the last state (a run must stay visible)
  const load = () =>
    request('/api/nozzle-expansion')
      .then(s => {
        setStale(false)
        setState(typeof s?.state === 'string' ? s : null)
      })
      .catch(() => setStale(true))
  const running = !!state && state.state !== 'idle'
  usePoll(load, running ? 2000 : 15000)
  if (!state) return null

  const s = Number(start),
    e = Number(end),
    d = Number(step)
  const valid =
    Number.isInteger(s) &&
    Number.isInteger(e) &&
    s >= 100 &&
    s <= 135 && // the printer's 140 °C reference must lie inside the table
    (d === 5 || d === 10) &&
    e <= 340 &&
    e >= s + 3 * d &&
    (e - s) % d === 0
  const steps = valid ? (e - s) / d + 1 : 0
  const minutes = Math.ceil((steps * (state.delay + SECONDS_PER_STEP) + SETUP_SECONDS) / 60)
  const active = state.override ?? state.factory
  const act = async (fn: () => Promise<unknown>) => {
    if (inFlight.current) return
    inFlight.current = true
    setBusy(true)
    try {
      await fn()
      await load()
    } catch (err) {
      notify(tpl('common.rejected_error', { error: errText(err) }), 'error')
    } finally {
      inFlight.current = false
      setBusy(false)
    }
  }
  const begin = () =>
    act(async () => {
      if (!(await ask(tpl('nozzle.start_confirm', { end: e }), true))) return
      setRefused('')
      try {
        await lanRequest('/api/nozzle-expansion/start', code, { method: 'POST', body: `${s}:${e}:${d}` })
      } catch (err) {
        setRefused(errText(err))
        throw err
      }
      setChecks([false, false, false])
    })
  const restart = () =>
    act(async () => {
      if (!(await ask(t('nozzle.restart_confirm'), true))) return
      await post('/api/nozzle-expansion/restart', 'RESTART')
      notify(t('nozzle.restarting'))
    })
  const factory = () =>
    act(async () => {
      if (!(await ask(t('nozzle.factory_confirm'), true))) return
      await post('/api/nozzle-expansion/factory', 'FACTORY')
    })

  const runStep =
    state.target && state.target > 0
      ? Math.min(
          Math.max(Math.round((state.target - state.start) / state.step) + 1, 1),
          (state.end - state.start) / state.step + 1
        )
      : 1
  const lines = [
    state.factory && { table: state.factory, label: t('nozzle.legend_factory'), class: 'text-muted' },
    state.override && { table: state.override, label: t('nozzle.legend_saved'), class: 'text-cyan' },
    state.measured &&
      state.result !== 'saved' && { table: state.measured, label: t('nozzle.legend_last'), class: 'text-amber' },
  ].filter(Boolean) as { table: Table; label: string; class: string }[]
  const reference = state.reference || 140
  const fmt = (n: number, digits = 2) => n.toFixed(digits)

  return (
    <div class="max-w-4xl">
      <Card data-nozzle-expansion>
        <CardHead icon="temp" title="nozzle.title" />
        <p class="text-muted">{tpl('nozzle.hint', { ref: reference })}</p>
        {!state.available ? (
          <Warn>{t('nozzle.unavailable')}</Warn>
        ) : (
          active && (
            <p class="mt-2 text-xs">
              {tpl(
                state.pending_restart
                  ? state.override
                    ? 'nozzle.next_saved'
                    : 'nozzle.next_factory'
                  : state.override
                    ? 'nozzle.active_saved'
                    : 'nozzle.active_factory',
                {
                  start: active.start,
                  end: active.start + (active.values.length - 1) * active.diff,
                  slope: fmt(slope(active)),
                }
              )}
            </p>
          )
        )}
        {lines.length > 0 && <Chart lines={lines} reference={reference} />}
      </Card>

      {state.available && (
        <Card class="mt-3.5">
          <CardHead icon="target" title="nozzle.run_title" />
          <div class="grid gap-3 cc2-md:grid-cols-3">
            {(
              [
                ['nozzle-start', 'nozzle.start_temp', start, setStart, 100, 135, 5],
                ['nozzle-end', 'nozzle.end_temp', end, setEnd, 150, 340, 5],
                ['nozzle-step', 'nozzle.step', step, setStep, 5, 10, 5],
              ] as const
            ).map(([id, label, value, set, min, max, inc]) => (
              <label key={id} htmlFor={id}>
                {t(label)}
                <Input
                  id={id}
                  class="mt-1"
                  type="number"
                  min={min}
                  max={max}
                  step={inc}
                  value={value}
                  disabled={busy || running}
                  onInput={ev => set(ev.currentTarget.value)}
                />
              </label>
            ))}
          </div>
          <p class="mt-2 text-xs text-muted">
            {valid
              ? tpl('nozzle.estimate', { steps, minutes, touches: steps * state.samples, end: e })
              : t('nozzle.invalid')}
          </p>
          <fieldset class="mt-3 grid gap-1.5" disabled={busy || running}>
            {(['nozzle.check_unloaded', 'nozzle.check_clean', 'nozzle.check_plate'] as const).map((key, i) => (
              <label key={key} class="flex items-center gap-2">
                <input
                  type="checkbox"
                  checked={checks[i]}
                  onChange={ev => setChecks(c => c.map((v, j) => (j === i ? ev.currentTarget.checked : v)))}
                />
                {t(key)}
              </label>
            ))}
          </fieldset>
          <div class="mt-3 grid gap-3 cc2-md:grid-cols-3">
            <label htmlFor="nozzle-lan">
              {t('nozzle.lan_code')}
              <PasswordInput
                id="nozzle-lan"
                class="mt-1"
                value={code}
                autoComplete="off"
                disabled={busy}
                onInput={ev => setCode(ev.currentTarget.value)}
              />
            </label>
          </div>
          <div class="mt-3 flex flex-wrap items-center gap-2">
            <Button
              disabled={busy || running || !valid || !checks.every(Boolean) || !code.trim()}
              onClick={() => void begin()}
            >
              {t('nozzle.start')}
            </Button>
            <span role="status" aria-live="polite" class="text-xs">
              {state.state === 'running'
                ? tpl('nozzle.running', {
                    step: runStep,
                    steps: (state.end - state.start) / state.step + 1,
                    nozzle: state.nozzle === null ? '—' : fmt(state.nozzle, 0),
                    target: state.target === null ? '—' : fmt(state.target, 0),
                    elapsed: duration(state.elapsed),
                  })
                : state.state !== 'idle'
                  ? t(STAGES[state.state])
                  : ''}
            </span>
            {refused && !running && (
              <p role="alert" class="basis-full text-xs text-red" data-nozzle-refused>
                {tpl('nozzle.start_refused', { error: refused })}
              </p>
            )}
          </div>
          {running && <Notice icon="info">{t('nozzle.estop_hint')}</Notice>}
          {stale && <p class="mt-2 text-xs text-amber">{t('nozzle.stale')}</p>}
          {!running && state.result === 'saved' && (
            <p class="mt-2 text-cyan">
              {tpl('nozzle.saved', { slope: fmt(state.slope_um), residual: fmt(state.residual_um, 1) })}
              {state.detail && <span class="block text-xs text-amber">{state.detail}</span>}
            </p>
          )}
          {!running && (state.result === 'rejected' || state.result === 'failed' || state.result === 'interrupted') && (
            <p class="mt-2 break-words text-amber" role="alert">
              {tpl(RESULTS[state.result], { detail: state.detail })}
            </p>
          )}
          {state.pending_restart && (
            <div class="mt-2 flex flex-wrap items-center gap-2">
              <p class="text-xs text-cyan">
                {t(
                  state.result === 'rejected' || state.result === 'failed' || state.result === 'interrupted'
                    ? 'nozzle.restart_discard'
                    : 'nozzle.restart_needed'
                )}
              </p>
              <Button disabled={busy || running || state.restart === 'restarting'} onClick={() => void restart()}>
                {t('nozzle.restart')}
              </Button>
            </div>
          )}
          {state.override && !running && (
            <Button class="mt-2" disabled={busy || !state.editable} onClick={() => void factory()}>
              {t('nozzle.factory')}
            </Button>
          )}
        </Card>
      )}
    </div>
  )
}
