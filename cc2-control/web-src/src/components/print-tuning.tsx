import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { control, notify } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { printer, view } from '@/lib/state'

type Kind = 'speed' | 'flow' | 'pressure-advance'
const percent = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : null)
const display = (v: number | null) => (v === null ? '—' : String(Math.round(v * 10) / 10))

const TuningForm = ({
  d,
  rev,
  ok,
  compact,
  sliders,
  pressureAdvance,
}: {
  d: any
  rev: number
  ok: boolean
  compact: boolean
  sliders: boolean
  pressureAdvance: boolean
}) => {
  const [draft, setDraft] = useState({ speed: '', flow: '', 'pressure-advance': '' })
  const [pending, setPending] = useState<{ kind: Kind; value: number; rev: number } | null>(null)
  const sending = useRef(false)
  const [posting, setPosting] = useState(false)
  const actual = {
    speed: percent(d?.tuning?.speed_percent),
    flow: percent(d?.tuning?.flow_percent),
    'pressure-advance': percent(d?.tuning?.pressure_advance),
  }
  const live = percent(d?.tuning?.live_velocity)
  const v = view(d)
  const active =
    ['printing', 'paused'].includes(d?.print?.state) &&
    d?.connected &&
    d?.machine?.status === 2 &&
    v.active &&
    (v.printing || v.paused)
  const fresh = Number(d?.last_message_age) >= 0 && Number(d?.last_message_age) <= 15
  const ready = ok && active && fresh
  const nativeState = d?.print?.native?.state
  const pressureReady =
    ok &&
    fresh &&
    d?.connected &&
    ((d?.machine?.status === 1 && ['standby', 'complete'].includes(nativeState)) ||
      (active && ['printing', 'paused'].includes(nativeState)))
  const kinds: Kind[] = pressureAdvance ? ['speed', 'flow', 'pressure-advance'] : ['speed', 'flow']

  useEffect(() => {
    if (!pending) return
    const timeout = window.setTimeout(() => {
      sending.current = false
      setPending(null)
      notify(t('tuning.not_confirmed'), 'error')
    }, 8000)
    return () => clearTimeout(timeout)
  }, [pending])

  useEffect(() => {
    const observed = pending ? actual[pending.kind] : null
    if (
      pending &&
      rev > pending.rev &&
      observed !== null &&
      Math.abs(observed - pending.value) < (pending.kind === 'pressure-advance' ? 0.0000005 : 0.05)
    ) {
      sending.current = false
      setDraft(s => ({ ...s, [pending.kind]: '' }))
      setPending(null)
    }
  }, [rev, pending, actual.speed, actual.flow, actual['pressure-advance']])

  const apply = async (kind: Kind, value: number) => {
    if (!(kind === 'pressure-advance' ? pressureReady : ready) || actual[kind] === null || sending.current) return
    sending.current = true
    setPosting(true)
    if (await control(`tune:${kind}:${value}`)) {
      setPending({ kind, value, rev: printer.get().rev })
    } else {
      sending.current = false
    }
    setPosting(false)
  }

  return (
    <Card class="cc2-tuning">
      <CardHead icon="settings" title="tuning.title" end={pending || posting ? t('tuning.waiting') : undefined} />
      <div class={compact ? 'cc2-tuning-fields grid gap-3' : 'cc2-tuning-fields grid gap-4 cc2-sm:grid-cols-2'}>
        {kinds.map(kind => {
          const pa = kind === 'pressure-advance'
          const label = pa ? 'tuning.pressure_advance' : kind === 'speed' ? 'tuning.speed' : 'tuning.flow'
          const observed = pa
            ? actual[kind] === null
              ? '—'
              : String(Math.round(actual[kind] * 1e6) / 1e6)
            : display(actual[kind])
          const min = pa ? 0 : kind === 'speed' ? 25 : 50
          const max = pa ? 1 : kind === 'speed' ? 200 : 150
          const value = Number(draft[kind])
          const valid =
            (pa ? /^\d+(?:\.\d{1,6})?$/.test(draft[kind]) : /^\d+$/.test(draft[kind]) && Number.isInteger(value)) &&
            value >= min &&
            value <= max
          const disabled = !(pa ? pressureReady : ready) || actual[kind] === null || pending !== null || posting
          return (
            <div key={kind} class="cc2-tuning-field min-w-0">
              {sliders ? (
                <div class="cc2-tuning-heading">
                  <label for={`tune-${kind}`} class="font-medium">
                    {t(label)}
                  </label>
                  <span class="text-xs text-cyan">
                    {tpl(pa ? 'tuning.actual_pa' : 'tuning.actual', { value: observed })}
                  </span>
                  <input
                    type="range"
                    class="cc2-tuning-slider"
                    min={min}
                    max={max}
                    step={pa ? 0.001 : 1}
                    value={Math.max(min, Math.min(max, valid ? value : (actual[kind] ?? (pa ? 0 : 100))))}
                    aria-label={t(label)}
                    disabled={disabled}
                    onInput={e => setDraft(s => ({ ...s, [kind]: e.currentTarget.value }))}
                  />
                </div>
              ) : (
                <div class={compact ? 'mb-2 flex flex-wrap items-center justify-between gap-1' : ''}>
                  <label for={`tune-${kind}`} class="mb-1 block font-medium">
                    {t(label)}
                  </label>
                  <div class="mb-2 text-sm text-cyan">
                    {tpl(pa ? 'tuning.actual_pa' : 'tuning.actual', { value: observed })}
                  </div>
                </div>
              )}
              <form
                class="flex flex-wrap gap-2"
                onSubmit={e => {
                  e.preventDefault()
                  if (valid && !disabled) void apply(kind, value)
                }}
              >
                <Input
                  id={`tune-${kind}`}
                  type="number"
                  min={min}
                  max={max}
                  step={pa ? 0.000001 : 1}
                  class={pa ? 'cc2-pressure-value w-28' : 'w-24'}
                  value={draft[kind]}
                  placeholder={observed}
                  disabled={disabled}
                  onInput={e => setDraft(s => ({ ...s, [kind]: e.currentTarget.value }))}
                />
                <Button type="submit" disabled={disabled || !valid}>
                  {t('tuning.apply')}
                </Button>
                <Button type="button" disabled={disabled} onClick={() => void apply(kind, pa ? 0 : 100)}>
                  {t(pa ? 'tuning.disable_pa' : 'tuning.reset')}
                </Button>
              </form>
              {!compact && !pa && <small class="mt-1 block text-muted">{tpl('tuning.range', { min, max })}</small>}
            </div>
          )
        })}
      </div>
      {pressureAdvance && <p class="mt-2 text-xs text-muted">{t('tuning.pa_note')}</p>}
      {!compact && <div class="mt-3 text-sm">{tpl('tuning.live_velocity', { value: display(live) })}</div>}
      {!sliders && <p class="mt-2 text-xs text-muted">{t(ready ? 'tuning.manual_note' : 'tuning.unavailable')}</p>}
    </Card>
  )
}

export const PrintTuning = ({
  compact = false,
  sliders = false,
  pressureAdvance = false,
}: {
  compact?: boolean
  sliders?: boolean
  pressureAdvance?: boolean
}) => {
  const { data, rev, ok } = printer.use()
  return (
    <TuningForm
      key={String(data?.print?.uuid || data?.print?.filename || 'idle')}
      d={data}
      rev={rev}
      ok={ok}
      compact={compact}
      sliders={sliders}
      pressureAdvance={pressureAdvance}
    />
  )
}
