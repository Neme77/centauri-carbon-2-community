import { useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input, PasswordInput } from '@/components/ui/field'
import { ask } from '@/lib/confirm'
import { errText, request } from '@/lib/api'
import { t, type Key } from '@/lib/i18n'
import { duration } from '@/lib/format'
import { printer, refreshPrinter } from '@/lib/state'

const stages: Record<string, Key> = {
  off: 'drying.off',
  checking: 'drying.checking',
  arming: 'drying.checking',
  heating: 'drying.heating',
  holding: 'drying.holding',
  stopping: 'drying.stopping',
  completed: 'drying.completed',
  interrupted: 'drying.interrupted',
}
export const BedDrying = () => {
  const { data, ok } = printer.use()
  const session = data?.bed_drying
  const [temp, setTemp] = useState('70'),
    [minutes, setMinutes] = useState('360')
  const [code, setCode] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('')
  if (!session?.enabled && !session?.active) return null
  const valid =
    Number.isInteger(Number(temp)) &&
    Number(temp) >= 30 &&
    Number(temp) <= 90 &&
    Number.isInteger(Number(minutes)) &&
    Number(minutes) >= 1 &&
    Number(minutes) <= 720
  const startable =
    ok &&
    data?.connected &&
    data?.machine?.status === 1 &&
    session?.fresh &&
    data?.extruder?.temperature <= 50 &&
    data?.heater_bed?.temperature <= 50 &&
    data?.extruder?.target === 0 &&
    data?.heater_bed?.target === 0 &&
    !session.active
  const send = async (action: 'start' | 'stop') => {
    if (busy || !code.trim()) return
    if (action === 'start' && !(await ask(t('drying.start_confirm')))) return
    setBusy(true)
    setError('')
    try {
      await request(`/api/bed-drying/${action}`, {
        method: 'POST',
        headers: { 'X-CC2-LAN': code.trim() },
        body: action === 'start' ? `${Number(temp)}:${Number(minutes)}` : '',
      })
      await refreshPrinter()
    } catch (e) {
      setError(errText(e))
    } finally {
      setBusy(false)
    }
  }
  return (
    <Card class="mt-3.5" data-bed-drying>
      <CardHead icon="temp" title="drying.title" />
      <p class="text-muted">{t('drying.hint')}</p>
      <p class="mt-2 text-xs text-muted">{t('drying.guard')}</p>
      <div class="mt-3 grid gap-3 cc2-md:grid-cols-3">
        <label htmlFor="drying-temperature">
          {t('common.bed_temperature_c')}
          <Input
            id="drying-temperature"
            class="mt-1"
            type="number"
            min={30}
            max={90}
            step={1}
            value={temp}
            disabled={busy || session.active}
            onInput={e => setTemp(e.currentTarget.value)}
          />
        </label>
        <label htmlFor="drying-duration">
          {t('drying.minutes')}
          <Input
            id="drying-duration"
            class="mt-1"
            type="number"
            min={1}
            max={720}
            step={1}
            value={minutes}
            disabled={busy || session.active}
            onInput={e => setMinutes(e.currentTarget.value)}
          />
        </label>
        <label htmlFor="drying-lan">
          {t('experiments.lan_code')}
          <PasswordInput
            id="drying-lan"
            class="mt-1"
            value={code}
            autoComplete="off"
            disabled={busy}
            onInput={e => setCode(e.currentTarget.value)}
          />
        </label>
      </div>
      <div class="mt-3 flex flex-wrap items-center gap-2">
        <Button disabled={busy || !startable || !valid || !code.trim()} onClick={() => void send('start')}>
          {t('drying.start')}
        </Button>
        <Button disabled={busy || !ok || !session.active || !code.trim()} onClick={() => void send('stop')}>
          {t('drying.stop')}
        </Button>
        <span role="status" aria-live="polite">
          {t(!ok ? 'drying.unavailable' : stages[session.state] || 'drying.interrupted')}
        </span>
      </div>
      <p class="mt-2 text-muted" data-drying-readback>
        {session.fresh && ok ? `${session.bed.toFixed(1)} / ${session.target.toFixed(0)} °C` : t('drying.unavailable')}
        {session.state === 'holding' && ` · ${t('drying.remaining')}: ${duration(session.remaining)}`}
      </p>
      {session.reason !== 'none' && (
        <p class="mt-2 text-muted">{t(session.reason === 'elapsed' ? 'drying.expired' : 'drying.interrupted_hint')}</p>
      )}
      {error && (
        <p class="mt-2 break-words text-red" role="alert">
          {error}
        </p>
      )}
    </Card>
  )
}
