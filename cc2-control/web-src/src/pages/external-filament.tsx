import { useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { FilamentMaterial } from '@/components/filament-material'
import { control, post, errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { canvasColour, refreshCanvas } from '@/lib/canvas'
import { externalFeed, refreshExternalFeed } from '@/lib/external-feed'
import { printer } from '@/lib/state'
import { usePoll } from '@/lib/poll'
import { t } from '@/lib/i18n'
const refresh = refreshExternalFeed
export const ExternalFilament = () => {
  const state = externalFeed.use(),
    machine = printer.use()
  const [editing, setEditing] = useState(false),
    [busy, setBusy] = useState(false)
  usePoll(refresh, 5000)
  if (!state.available) return null
  const idle = state.idle && machine.ok && machine.data?.connected && machine.data?.machine?.status === 1 && !busy
  const choose = async (source: 'canvas' | 'external') => {
    if (!state.can_select || busy || !(await ask(t('feed.select_confirm')))) return
    setBusy(true)
    try {
      await post('/api/filament-source', source)
      await Promise.all([refresh(), refreshCanvas()])
    } catch (e) {
      notify(errText(e), 'error')
    } finally {
      setBusy(false)
    }
  }
  const send = async (action: 'load' | 'unload') => {
    if (!idle || !(await ask(t(action === 'load' ? 'filament.load_confirm' : 'filament.unload_confirm')))) return
    setBusy(true)
    try {
      await control(`external:${action}`)
      await refresh()
    } finally {
      setBusy(false)
    }
  }
  return (
    <Card class="mt-3.5" data-external-filament>
      <CardHead icon="canvas" title="filament.external" />
      {state.experiment && (
        <div class="mb-3" data-filament-source>
          <fieldset class="flex min-w-0 flex-wrap items-center gap-2" aria-label={t('feed.source')}>
            <span class="text-muted">{t('feed.source')}</span>
            <Button
              variant={state.source === 'canvas' ? 'primary' : 'default'}
              aria-pressed={state.source === 'canvas'}
              disabled={!state.can_select || busy}
              onClick={() => choose('canvas')}
            >
              {t('common.canvas')}
            </Button>
            <Button
              variant={state.source === 'external' ? 'primary' : 'default'}
              aria-pressed={state.source === 'external'}
              disabled={!state.can_select || busy}
              onClick={() => choose('external')}
            >
              {t('filament.external')}
            </Button>
          </fieldset>
          <p class="mt-2 text-xs text-muted">{t('feed.selection_hint')}</p>
        </div>
      )}
      <div class="flex flex-wrap items-center justify-between gap-3">
        <div>
          <strong class="inline-flex items-center gap-2">
            <span
              class="inline-block h-3 w-3 rounded-full border border-edge"
              aria-hidden="true"
              style={{ backgroundColor: canvasColour(state.filament?.color, 0) }}
            />
            {state.filament?.detailed_type || state.filament?.type || '—'}
          </strong>
          <p class="text-xs text-muted">{state.filament?.manufacturer || '—'}</p>
        </div>
        <div class="flex flex-wrap gap-2">
          <Button disabled={!idle} onClick={() => send('load')}>
            {t('canvas.load')}
          </Button>
          <Button disabled={!idle || state.can_unload === false} onClick={() => send('unload')}>
            {t('canvas.unload')}
          </Button>
          <Button disabled={!idle || !state.filament} onClick={() => setEditing(true)}>
            {t('filament.edit')}
          </Button>
        </div>
      </div>
      {editing && idle && (
        <FilamentMaterial filament={state.filament} onClose={() => setEditing(false)} onSaved={refresh} />
      )}
    </Card>
  )
}
