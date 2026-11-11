import { useRef, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { errText, post, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { printer } from '@/lib/state'

type FloorStatus = { installed: boolean; ready: boolean; printing: boolean; minimum: number | null }

export const CavityFanFloor = () => {
  const link = printer.use()
  const [status, setStatus] = useState<FloorStatus | null>(null)
  const [value, setValue] = useState('60')
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState('')
  const sending = useRef(false)
  const edited = useRef(false)
  const refresh = async () => {
    try {
      const next = await request('/api/cavity-fan-floor')
      setStatus(next)
      if (!edited.current && next.minimum != null) setValue(String(next.minimum))
    } catch {
      setStatus(null)
    }
  }
  usePoll(refresh, 5000)
  if (!status?.installed) return null
  const data = link.data
  const canApply =
    status.ready &&
    status.printing &&
    link.ok &&
    data?.connected &&
    data?.machine?.status === 2 &&
    data?.last_message_age >= 0 &&
    data.last_message_age <= 15
  const send = async (automatic = false) => {
    if (sending.current || (!automatic && !canApply)) return
    const percent = automatic ? 0 : Number(value)
    if (!automatic && (!value.trim() || !Number.isInteger(percent) || percent < 0 || percent > 100)) {
      setMessage(t('control.invalid_fan_percent'))
      return
    }
    sending.current = true
    setBusy(true)
    setMessage(t('fanfloor.pending'))
    try {
      await post('/api/cavity-fan-floor', String(percent))
      edited.current = false
      await refresh()
      setMessage(t(percent === 0 ? 'fanfloor.auto_accepted' : 'fanfloor.accepted'))
    } catch (error) {
      setMessage(errText(error))
    } finally {
      sending.current = false
      setBusy(false)
    }
  }
  return (
    <div class="cc2-cavity-floor mt-3.5 border-t border-edge pt-3">
      <p class="text-xs font-medium">{t('fanfloor.title')}</p>
      <p class="mt-2 text-xs text-muted">{t('fanfloor.help')}</p>
      <div class="mt-2 flex flex-wrap items-center gap-2">
        <Input
          class="w-18 text-center"
          type="number"
          min="0"
          max="100"
          step="1"
          aria-label={t('fanfloor.minimum_label')}
          value={value}
          disabled={!canApply || busy}
          onInput={event => {
            edited.current = true
            setValue(event.currentTarget.value)
          }}
        />
        <span class="text-xs">%</span>
        <Button disabled={!canApply || busy} onClick={() => send()}>
          {t('fanfloor.apply')}
        </Button>
        <Button disabled={!status.ready || status.minimum == null || busy} onClick={() => send(true)}>
          {t('fanfloor.automatic')}
        </Button>
      </div>
      <p class="mt-2 text-xs text-muted">
        {status.minimum == null ? t('fanfloor.automatic') : tpl('fanfloor.requested', { percent: status.minimum })}
      </p>
      <p role="status" aria-live="polite" class="mt-2 min-h-9 text-xs">
        {message || (!status.ready ? t('fanfloor.not_ready') : !canApply ? t('fanfloor.printing_required') : '')}
      </p>
    </div>
  )
}
