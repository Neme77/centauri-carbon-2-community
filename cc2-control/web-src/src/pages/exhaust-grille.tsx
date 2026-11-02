import { useRef, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { errText, post } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { printer } from '@/lib/state'

export const ExhaustGrille = () => {
  const link = printer.use()
  const d = link.data
  const native = d?.print?.native?.state
  const allowed =
    link.ok &&
    d?.connected &&
    d.last_message_age >= 0 &&
    d.last_message_age <= 15 &&
    d.machine?.status === 1 &&
    (native === 'standby' || native === 'complete') &&
    d.extruder?.target === 0 &&
    d.heater_bed?.target === 0 &&
    !d.bed_drying?.active
  const sending = useRef(false)
  const [pending, setPending] = useState(false)
  const [accepted, setAccepted] = useState<'open' | 'close' | null>(null)
  const [error, setError] = useState('')
  const send = async (direction: 'open' | 'close') => {
    if (!allowed || sending.current) return
    sending.current = true
    setPending(true)
    setAccepted(null)
    setError('')
    try {
      await post('/api/control', `grille:${direction}`)
      setAccepted(direction)
    } catch (e) {
      setError(errText(e))
    } finally {
      sending.current = false
      setPending(false)
    }
  }
  return (
    <div class="cc2-exhaust-grille mt-3 border-t border-edge pt-3">
      <div class="grid grid-cols-2 gap-2">
        <Button disabled={!allowed || pending} onClick={() => send('open')}>
          {t('grille.open')}
        </Button>
        <Button disabled={!allowed || pending} onClick={() => send('close')}>
          {t('grille.close')}
        </Button>
      </div>
      <p class="mt-2 text-xs text-muted">{t('grille.native_policy')}</p>
      <p role="status" aria-live="polite" class="mt-2 min-h-9 text-xs">
        {pending
          ? t('grille.sending')
          : error
            ? tpl('common.rejected_error', { error })
            : accepted
              ? t(accepted === 'open' ? 'grille.open_accepted' : 'grille.close_accepted')
              : !allowed
                ? t('grille.idle_required')
                : ''}
      </p>
    </div>
  )
}
