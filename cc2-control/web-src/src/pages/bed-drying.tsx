import { lanRequest, useLanCode } from '@/lib/lan-code'
import { useEffect, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input, PasswordInput } from '@/components/ui/field'
import { ask } from '@/lib/confirm'
import { errText } from '@/lib/api'
import { t, tpl, type Key } from '@/lib/i18n'
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
  handed_off: 'preheat.handed_off',
}
const preheatEnds: Record<string, Key> = {
  heat_timeout: 'preheat.timeout',
  chamber: 'preheat.no_chamber',
  print: 'preheat.handed_off_hint',
}
export const BedDrying = () => {
  const [code, setCode] = useLanCode()
  const { data, ok } = printer.use()
  const session = data?.bed_drying
  const [temp, setTemp] = useState('70'),
    [minutes, setMinutes] = useState('360')
  const [keepTemp, setKeepTemp] = useState('0'),
    [keepMinutes, setKeepMinutes] = useState('0')
  const [chamber, setChamber] = useState('45'),
    [preheatBed, setPreheatBed] = useState('100'),
    [hold, setHold] = useState('30'),
    [choice, setChoice] = useState<'bed' | 'preheat'>('bed')
  const [busy, setBusy] = useState(false),
    [error, setError] = useState('')
  // Follow the mode of a session that is running or has just ended (also after a reload).
  useEffect(() => {
    if (session?.mode && session.state !== 'off') setChoice(session.mode === 'preheat' ? 'preheat' : 'bed')
  }, [session?.mode, session?.active])
  if (!session?.enabled && !session?.active) return null
  const preheat = session.active ? session.mode === 'preheat' : choice === 'preheat'
  const bedLimit = Number(session.bed_limit) >= 60 ? Math.min(100, Number(session.bed_limit)) : 100
  const whole = (v: string, min: number, max: number) =>
    Number.isInteger(Number(v)) && Number(v) >= min && Number(v) <= max
  const keep = Number(keepTemp)
  const keepM = Number(keepMinutes)
  const keepValid = keep === 0 || (whole(keepTemp, 30, Number(temp)) && whole(keepMinutes, 1, 720))
  const valid = preheat
    ? whole(chamber, 30, 55) && whole(preheatBed, 60, bedLimit) && whole(hold, 1, 240)
    : whole(temp, 30, 90) && whole(minutes, 1, 720) && keepValid
  const startable =
    ok &&
    data?.connected &&
    data?.machine?.status === 1 &&
    session?.fresh &&
    data?.extruder?.temperature <= 50 &&
    (preheat || data?.heater_bed?.temperature <= 50) &&
    (!preheat || (Number.isFinite(session.chamber) && session.chamber < Number(chamber))) &&
    data?.extruder?.target === 0 &&
    data?.heater_bed?.target === 0 &&
    !session.active
  const manualStopped =
    ok &&
    data?.connected &&
    !session.active &&
    session.state === 'interrupted' &&
    session.reason === 'manual' &&
    session.fresh &&
    session.target === 0
  const send = async (action: 'start' | 'stop') => {
    if (busy || !code.trim()) return
    const question = preheat ? tpl('preheat.start_confirm', { bed: Number(preheatBed) }) : t('drying.start_confirm')
    if (action === 'start' && !(await ask(question))) return
    setBusy(true)
    setError('')
    try {
      const path = action === 'stop' ? 'stop' : preheat ? 'preheat' : 'start'
      await lanRequest(`/api/bed-drying/${path}`, code, {
        method: 'POST',
        body:
          action === 'stop'
            ? ''
            : preheat
              ? `${Number(preheatBed)}:${Number(chamber)}:${Number(hold)}`
              : keep > 0
                ? `${Number(temp)}:${Number(minutes)}:${keep}:${keepM}`
                : `${Number(temp)}:${Number(minutes)}`,
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
      <CardHead icon="temp" title={preheat ? 'preheat.title' : 'drying.title'} />
      <fieldset class="mb-3 flex flex-wrap gap-2 border-0 p-0" aria-label={t('preheat.mode')}>
        {(['bed', 'preheat'] as const).map(mode => (
          <Button
            key={mode}
            variant={(mode === 'preheat') === preheat ? 'active' : 'default'}
            aria-pressed={(mode === 'preheat') === preheat}
            disabled={busy || session.active}
            onClick={() => setChoice(mode)}
          >
            {t(mode === 'preheat' ? 'preheat.mode_preheat' : 'preheat.mode_drying')}
          </Button>
        ))}
      </fieldset>
      <p class="text-muted">{t(preheat ? 'preheat.hint' : 'drying.hint')}</p>
      <p class="mt-2 text-xs text-muted">{preheat ? tpl('preheat.guard', { bed: bedLimit }) : t('drying.guard')}</p>
      {preheat && (
        <p class="mt-2 text-xs text-amber" data-preheat-ambient>
          {t('preheat.ambient')}
        </p>
      )}
      {preheat ? (
        <div class="mt-3 grid gap-3 cc2-md:grid-cols-4">
          <label htmlFor="preheat-chamber">
            {t('preheat.chamber_target')}
            <Input
              id="preheat-chamber"
              class="mt-1"
              type="number"
              min={30}
              max={55}
              step={1}
              value={chamber}
              disabled={busy || session.active}
              onInput={e => setChamber(e.currentTarget.value)}
            />
          </label>
          <label htmlFor="preheat-bed">
            {t('common.bed_temperature_c')}
            <Input
              id="preheat-bed"
              class="mt-1"
              type="number"
              min={60}
              max={bedLimit}
              step={1}
              value={preheatBed}
              disabled={busy || session.active}
              onInput={e => setPreheatBed(e.currentTarget.value)}
            />
          </label>
          <label htmlFor="preheat-hold">
            {t('preheat.hold')}
            <Input
              id="preheat-hold"
              class="mt-1"
              type="number"
              min={1}
              max={240}
              step={1}
              value={hold}
              disabled={busy || session.active}
              onInput={e => setHold(e.currentTarget.value)}
            />
          </label>
          <label htmlFor="preheat-lan">
            {t('experiments.lan_code')}
            <PasswordInput
              id="preheat-lan"
              class="mt-1"
              value={code}
              autoComplete="off"
              disabled={busy}
              onInput={e => setCode(e.currentTarget.value)}
            />
          </label>
        </div>
      ) : (
        <>
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
          <div class="mt-3 grid gap-3 cc2-md:grid-cols-2">
            <label htmlFor="drying-keep-temp">
              {t('drying.keep_temp')}
              <Input
                id="drying-keep-temp"
                class="mt-1"
                type="number"
                min={0}
                max={90}
                step={1}
                value={keepTemp}
                disabled={busy || session.active}
                onInput={e => setKeepTemp(e.currentTarget.value)}
              />
            </label>
            <label htmlFor="drying-keep-minutes">
              {t('drying.keep_minutes')}
              <Input
                id="drying-keep-minutes"
                class="mt-1"
                type="number"
                min={0}
                max={720}
                step={1}
                value={keepMinutes}
                disabled={busy || session.active || keep === 0}
                onInput={e => setKeepMinutes(e.currentTarget.value)}
              />
            </label>
          </div>
        </>
      )}
      <div class="mt-3 flex flex-wrap items-center gap-2">
        <Button disabled={busy || !startable || !valid || !code.trim()} onClick={() => void send('start')}>
          {t(preheat ? 'preheat.start' : 'drying.start')}
        </Button>
        <Button disabled={busy || !ok || !session.active || !code.trim()} onClick={() => void send('stop')}>
          {t(preheat ? 'preheat.stop' : 'drying.stop')}
        </Button>
        <span role="status" aria-live="polite">
          {t(!ok ? 'drying.unavailable' : stages[session.state] || 'drying.interrupted')}
        </span>
      </div>
      <p class="mt-2 text-muted" data-drying-readback>
        {preheat &&
          Number.isFinite(session.chamber) &&
          `${t('preheat.chamber')} ${session.chamber.toFixed(1)} / ${session.active ? session.chamber_target : Number(chamber)} °C · `}
        {preheat && t('preheat.bed')}{' '}
        {session.fresh && ok ? `${session.bed.toFixed(1)} / ${session.target.toFixed(0)} °C` : t('drying.unavailable')}
        {preheat && session.active && ` · ${t('preheat.fan')} ${Math.round((session.fan / 255) * 100)} %`}
        {session.state === 'holding' &&
          ` · ${t(session.keeping ? 'drying.keeping' : 'drying.remaining')}: ${duration(session.remaining)}`}
      </p>
      {preheat && session.active && session.state === 'holding' && (
        <p
          class={`mt-2 font-semibold ${session.levelled ? 'text-amber' : 'text-green'}`}
          role="status"
          data-preheat-ready
        >
          {t(session.levelled ? 'preheat.levelled' : 'preheat.ready')}
        </p>
      )}
      {preheat &&
        session.active &&
        session.state === 'heating' &&
        Number.isFinite(session.plateau) &&
        session.plateau < session.chamber_target - 1 && (
          <p class="mt-2 text-amber" role="alert" data-preheat-short>
            {tpl('preheat.short', { plateau: Number(session.plateau).toFixed(0), target: session.chamber_target })}
          </p>
        )}
      {preheat && session.reason !== 'none' && preheatEnds[session.reason] ? (
        <p class="mt-2 text-muted">{t(preheatEnds[session.reason])}</p>
      ) : (
        session.reason !== 'none' && (
          <p class="mt-2 text-muted">
            {t(
              manualStopped
                ? 'drying.manual_stopped'
                : session.reason === 'elapsed'
                  ? 'drying.expired'
                  : session.reason === 'restore_timeout'
                    ? 'drying.restore_timeout'
                    : session.reason === 'aborted'
                      ? 'drying.aborted'
                      : 'drying.interrupted_hint'
            )}
          </p>
        )
      )}
      {error && (
        <p class="mt-2 break-words text-red" role="alert">
          {error}
        </p>
      )}
    </Card>
  )
}
