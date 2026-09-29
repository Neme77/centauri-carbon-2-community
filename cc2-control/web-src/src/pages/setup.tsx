import { useEffect, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { Dialog } from '@/components/ui/dialog'
import { errText, notify, post, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'

// First-run cover: asks for the printer LAN access code until the backend reports it configured.
export const SetupDialog = () => {
  const [open, setOpen] = useState(false),
    [code, setCode] = useState(''),
    [busy, setBusy] = useState(false),
    [state, setState] = useState(t('common.checking_configuration'))
  useEffect(() => {
    request('/api/setup')
      .then(d => setOpen(Boolean(d.required)))
      .catch(e => notify(tpl('setup.setup_check_failed_error', { error: errText(e) }), 'error'))
  }, [])
  if (!open) return null
  const submit = async (e: Event) => {
    e.preventDefault()
    const c = code.trim()
    if (!/^[A-Za-z0-9._-]{1,128}$/.test(c)) return setState(t('setup.use_only_letters_numbers_dot'))
    setBusy(true)
    try {
      await post('/api/setup', c)
      setCode('')
      setState(t('setup.saved_restarting_cc2_control_and'))
      for (let n = 0; n < 60; n++) {
        await new Promise(r => setTimeout(r, 1000))
        try {
          const s = await request('/api/setup')
          if (s.mqtt_registered && s.snapshot_received) {
            setState(t('setup.printer_and_canvas_synchronized'))
            return location.reload()
          }
        } catch {
          setState(t('setup.cc2_control_is_restarting'))
        }
      }
      setState(t('setup.saved_but_synchronization_is'))
    } catch (err) {
      setState(tpl('setup.connection_failed_error', { error: errText(err) }))
    } finally {
      setBusy(false)
    }
  }
  return (
    <Dialog locked>
      <form onSubmit={submit}>
        <h2 class="mb-2 text-2xl font-semibold">{t('setup.connect_cc2_control')}</h2>
        <p class="mb-4 leading-relaxed text-muted">{t('setup.enter_the_lan_access_code_shown')}</p>
        <Input
          type="password"
          autoComplete="off"
          required
          class="mb-3"
          placeholder={t('setup.lan_access_code')}
          value={code}
          onInput={e => setCode(e.currentTarget.value)}
          autofocus
        />
        <Button type="submit" variant="primary" wide disabled={busy}>
          {t('setup.connect_printer')}
        </Button>
        <div class="mt-3 min-h-5.5 text-xs text-green">{state}</div>
      </form>
    </Dialog>
  )
}
