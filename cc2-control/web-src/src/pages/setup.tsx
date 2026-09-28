import { useEffect, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { Dialog } from '@/components/ui/dialog'
import { errText, notify, post, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'

// First-run cover: asks for the printer LAN access code until the backend reports it configured.
export const SetupDialog = () => {
  const [open, setOpen] = useState(false), [code, setCode] = useState(''), [busy, setBusy] = useState(false), [state, setState] = useState('Checking configuration…')
  useEffect(() => {
    request('/api/setup').then(d => setOpen(Boolean(d.required))).catch(e => notify(tpl('Setup check failed: {error}', { error: errText(e) }), 'error'))
  }, [])
  if (!open) return null
  const submit = async (e: Event) => {
    e.preventDefault()
    const c = code.trim()
    if (!/^[A-Za-z0-9._-]{1,128}$/.test(c)) return setState('Use only letters, numbers, dot, underscore or hyphen.')
    setBusy(true)
    try {
      await post('/api/setup', c); setCode('')
      setState('Saved. Restarting CC2 Control and synchronizing Canvas…')
      for (let n = 0; n < 60; n++) {
        await new Promise(r => setTimeout(r, 1000))
        try {
          const s = await request('/api/setup')
          if (s.mqtt_registered && s.snapshot_received) { setState('Printer and Canvas synchronized.'); return location.reload() }
        } catch { setState('CC2 Control is restarting…') }
      }
      setState('Saved, but synchronization is taking longer than expected. Reload this page in a few seconds.')
    } catch (err) { setState(tpl('Connection failed: {error}', { error: errText(err) })) } finally { setBusy(false) }
  }
  return (
    <Dialog locked>
      <form onSubmit={submit}>
        <h2 class="mb-2 text-2xl font-semibold">{t('Connect CC2 Control')}</h2>
        <p class="mb-4 leading-relaxed text-muted">{t('Enter the LAN access code shown by the printer. It is stored locally on the CC2 and is required only for first configuration.')}</p>
        <Input type="password" autoComplete="off" required class="mb-3" placeholder={t('LAN access code')} value={code} onInput={e => setCode(e.currentTarget.value)} autofocus />
        <Button type="submit" variant="primary" wide disabled={busy}>{t('Connect printer')}</Button>
        <div class="mt-3 min-h-5.5 text-xs text-green">{t(state)}</div>
      </form>
    </Dialog>
  )
}
