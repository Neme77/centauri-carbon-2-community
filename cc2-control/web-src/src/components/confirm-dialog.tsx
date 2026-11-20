import { useEffect, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Dialog } from '@/components/ui/dialog'
import { confirmState } from '@/lib/confirm'
import { t } from '@/lib/i18n'
import { poll } from '@/lib/poll'

export const ConfirmDialog = () => {
  const req = confirmState.use().open
  // A live question is rendered again twice a second, so its values follow the printer and a note that no longer
  // applies goes away.
  const live = typeof req?.text === 'function'
  const [, setTick] = useState(0)
  useEffect(() => {
    if (!live) return
    return poll(() => setTick(n => n + 1), 500)
  }, [live])
  if (!req) return null
  const close = (ok: boolean) => {
    confirmState.set({ open: null })
    req.resolve(ok)
  }
  return (
    <Dialog onClose={() => close(false)} width={440}>
      <p class="whitespace-pre-line text-[15px] leading-relaxed [overflow-wrap:anywhere]" data-confirm-text>
        {typeof req.text === 'function' ? req.text() : req.text}
      </p>
      {req.details && (
        <details class="mt-3 rounded-md border border-edge px-3" data-confirm-details>
          <summary class="flex min-h-10 cursor-pointer items-center text-sm font-semibold">{req.details.label}</summary>
          <p class="whitespace-pre-line pb-3 text-xs leading-relaxed text-muted [overflow-wrap:anywhere]">
            {req.details.text}
          </p>
        </details>
      )}
      <div class="mt-5 flex justify-end gap-2.5">
        <Button onClick={() => close(false)} autofocus={req.danger}>
          {t('common.cancel')}
        </Button>
        <Button variant={req.danger ? 'danger' : 'primary'} onClick={() => close(true)} autofocus={!req.danger}>
          {t('common.confirm')}
        </Button>
      </div>
    </Dialog>
  )
}
