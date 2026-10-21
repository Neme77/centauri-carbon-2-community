import { useState } from 'preact/hooks'
import { Bell, X } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { t, tpl } from '@/lib/i18n'
import { health, openSettings } from '@/lib/state'
import { compatible, newer, status, updateCatalog, updateChannel } from '@/lib/updates'
import { cn } from '@/lib/utils'

export const UpdateNotice = () => {
  const [open, setOpen] = useState(false)
  const state = status.use().data
  const { index } = updateCatalog.use()
  const { value: channel } = updateChannel.use()
  const current = health.use().data?.version || ''
  const installed = newer(current, state?.installed || '') ? current : state?.installed || ''
  const release = state?.enabled ? compatible(index, state.firmware, channel)[0] : undefined
  const available = release && newer(release.version, installed)
  const label = available ? tpl('updates.notice_available', { version: release.version }) : t('updates.notifications')
  const visit = () => {
    setOpen(false)
    openSettings('updates')
  }
  return (
    <>
      <button
        type="button"
        data-update-notice
        aria-label={label}
        title={label}
        aria-haspopup={available ? 'dialog' : undefined}
        aria-expanded={available ? open : undefined}
        onClick={() => (available ? setOpen(true) : visit())}
        class={cn(
          'relative flex h-11 w-11 shrink-0 items-center justify-center rounded-md border border-edge hover:border-cyan',
          available ? 'text-cyan' : 'text-muted'
        )}
      >
        <Bell size={20} strokeWidth={1.5} />
        {available && (
          <i data-update-dot aria-hidden="true" class="absolute right-2 top-2 size-2 rounded-full bg-amber" />
        )}
      </button>
      {open && (
        <Dialog width={440} onClose={() => setOpen(false)}>
          <div class="mb-4 flex items-start justify-between gap-3">
            <DialogTitle>{t('updates.notifications')}</DialogTitle>
            <Button
              variant="ghost"
              class="shrink-0 px-2"
              aria-label={t('updates.dismiss')}
              onClick={() => setOpen(false)}
            >
              <X size={18} />
            </Button>
          </div>
          <p class="font-semibold text-cyan">{available ? label : t('updates.no_update')}</p>
          <p class="mt-2 text-muted">{t('updates.application_only')}</p>
          <p class="mt-2 text-muted">{t('updates.notice_manual')}</p>
          <div class="mt-5 flex flex-wrap justify-end gap-2">
            <Button onClick={() => setOpen(false)}>{t('updates.dismiss')}</Button>
            <Button variant="primary" onClick={visit}>
              {t('updates.open')}
            </Button>
          </div>
        </Dialog>
      )}
    </>
  )
}
