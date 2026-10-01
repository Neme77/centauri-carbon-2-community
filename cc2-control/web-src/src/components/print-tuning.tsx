import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { control, notify } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { printer, view } from '@/lib/state'

type Kind = 'speed' | 'flow'
const percent = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : null)
const display = (v: number | null) => (v === null ? '—' : String(Math.round(v * 10) / 10))

const TuningForm = ({ d, rev, ok }: { d: any; rev: number; ok: boolean }) => {
  const [draft, setDraft] = useState({ speed: '', flow: '' })
  const [pending, setPending] = useState<{ kind: Kind; value: number; rev: number } | null>(null)
  const sending = useRef(false)
  const [posting, setPosting] = useState(false)
  const actual = { speed: percent(d?.tuning?.speed_percent), flow: percent(d?.tuning?.flow_percent) }
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
    if (pending && rev > pending.rev && observed !== null && Math.abs(observed - pending.value) < 0.05) {
      sending.current = false
      setDraft(s => ({ ...s, [pending.kind]: '' }))
      setPending(null)
    }
  }, [rev, pending, actual.speed, actual.flow])

  const apply = async (kind: Kind, value: number) => {
    if (!ready || actual[kind] === null || sending.current) return
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
    <Card>
      <CardHead icon="settings" title="tuning.title" end={pending || posting ? t('tuning.waiting') : undefined} />
      <div class="grid gap-4 @min-[640px]/page:grid-cols-2">
        {(['speed', 'flow'] as const).map(kind => {
          const min = kind === 'speed' ? 25 : 50
          const max = kind === 'speed' ? 200 : 150
          const value = Number(draft[kind])
          const valid = /^\d+$/.test(draft[kind]) && Number.isInteger(value) && value >= min && value <= max
          const disabled = !ready || actual[kind] === null || pending !== null || posting
          return (
            <div key={kind} class="min-w-0">
              <label for={`tune-${kind}`} class="mb-1 block font-medium">
                {t(kind === 'speed' ? 'tuning.speed' : 'tuning.flow')}
              </label>
              <div class="mb-2 text-sm text-cyan">{tpl('tuning.actual', { value: display(actual[kind]) })}</div>
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
                  step="1"
                  class="w-24"
                  value={draft[kind]}
                  placeholder={display(actual[kind])}
                  disabled={disabled}
                  onInput={e => setDraft(s => ({ ...s, [kind]: e.currentTarget.value }))}
                />
                <Button type="submit" disabled={disabled || !valid}>
                  {t('tuning.apply')}
                </Button>
                <Button type="button" disabled={disabled} onClick={() => void apply(kind, 100)}>
                  {t('tuning.reset')}
                </Button>
              </form>
              <small class="mt-1 block text-muted">{tpl('tuning.range', { min, max })}</small>
            </div>
          )
        })}
      </div>
      <div class="mt-3 text-sm">{tpl('tuning.live_velocity', { value: display(live) })}</div>
      <p class="mt-2 text-xs text-muted">{t(ready ? 'tuning.manual_note' : 'tuning.unavailable')}</p>
    </Card>
  )
}

export const PrintTuning = () => {
  const { data, rev, ok } = printer.use()
  return <TuningForm key={String(data?.print?.uuid || data?.print?.filename || 'idle')} d={data} rev={rev} ok={ok} />
}
