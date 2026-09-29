import { Button } from '@/components/ui/button'
import { Dialog } from '@/components/ui/dialog'
import { confirmState } from '@/lib/confirm'
import { t } from '@/lib/i18n'

export const ConfirmDialog = () => {
  const req = confirmState.use().open
  if (!req) return null
  const close = (ok: boolean) => {
    confirmState.set({ open: null })
    req.resolve(ok)
  }
  return (
    <Dialog onClose={() => close(false)} width={440}>
      <p class="whitespace-pre-line text-[15px] leading-relaxed [overflow-wrap:anywhere]">{req.text}</p>
      <div class="mt-5 flex justify-end gap-2.5">
        <Button onClick={() => close(false)} autofocus={req.danger}>
          {t('Cancel')}
        </Button>
        <Button variant={req.danger ? 'danger' : 'primary'} onClick={() => close(true)} autofocus={!req.danger}>
          {t('Confirm')}
        </Button>
      </div>
    </Dialog>
  )
}
