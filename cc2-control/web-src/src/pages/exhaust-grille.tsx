import { useRef, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { errText, post } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { printer } from '@/lib/state'
import { grilleAllowed, grilleBusy } from '@/lib/grille'

export const ExhaustGrille = () => {
  const link = printer.use()
  const allowed = grilleAllowed(link)
  const waiting = grilleBusy.use().busy
  const sending = useRef(false)
  const [pending, setPending] = useState(false)
  const [accepted, setAccepted] = useState<'open' | 'close' | null>(null)
  const [error, setError] = useState('')
  const send = async (direction: 'open' | 'close') => {
    if (!allowed || sending.current || grilleBusy.get().busy) return
    grilleBusy.set({ busy: true })
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
      grilleBusy.set({ busy: false })
      sending.current = false
      setPending(false)
    }
  }
  return (
    <div class="cc2-exhaust-grille mt-3 border-t border-edge pt-3">
      <div class="grid grid-cols-2 gap-2">
        <Button disabled={!allowed || pending || waiting} onClick={() => send('open')}>
          {t('grille.open')}
        </Button>
        <Button disabled={!allowed || pending || waiting} onClick={() => send('close')}>
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
