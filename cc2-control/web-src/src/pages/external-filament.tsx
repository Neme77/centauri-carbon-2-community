import { useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { FilamentMaterial } from '@/components/filament-material'
import { control, request } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { canvas, refreshCanvas } from '@/lib/canvas'
import { printer } from '@/lib/state'
import { usePoll } from '@/lib/poll'
import { store } from '@/lib/store'
import { t } from '@/lib/i18n'
const external = store({ available: false, idle: false, filament: null as any })
const refresh = async () => {
  await refreshCanvas()
  if (canvas.get().model?.connected !== false) {
    external.set({ available: false, idle: false })
    return
  }
  try {
    external.set(await request('/api/external-filament'))
  } catch {
    external.set({ idle: false })
  }
}
export const ExternalFilament = () => {
  const state = external.use(),
    machine = printer.use()
  const [editing, setEditing] = useState(false),
    [busy, setBusy] = useState(false)
  usePoll(refresh, 5000)
  if (!state.available) return null
  const idle = state.idle && machine.ok && machine.data?.connected && machine.data?.machine?.status === 1 && !busy
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
      <div class="flex flex-wrap items-center justify-between gap-3">
        <div>
          <strong>{state.filament?.detailed_type || state.filament?.type || '—'}</strong>
          <p class="text-xs text-muted">{state.filament?.manufacturer || '—'}</p>
        </div>
        <div class="flex flex-wrap gap-2">
          <Button disabled={!idle} onClick={() => send('load')}>
            {t('canvas.load')}
          </Button>
          <Button disabled={!idle} onClick={() => send('unload')}>
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
