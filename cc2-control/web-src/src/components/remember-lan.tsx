import { useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { PasswordInput, Switch } from '@/components/ui/field'
import { errText, notify } from '@/lib/api'
import { forgetLanCode, lanRequest, rememberedLan, rememberLanCode } from '@/lib/lan-code'
import { t } from '@/lib/i18n'

export const RememberLan = () => {
  const saved = rememberedLan.use().code
  const [open, setOpen] = useState(false),
    [code, setCode] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('')
  const close = () => {
    setOpen(false)
    setCode('')
    setError('')
  }
  const save = async () => {
    if (busy || !code.trim()) return
    setBusy(true)
    setError('')
    try {
      await lanRequest('/api/access/verify', code)
      if (!rememberLanCode(code.trim())) {
        setError(t('settings.remember_storage_failed'))
        return
      }
      close()
      notify(t('settings.remember_saved'))
    } catch (e) {
      setError(errText(e))
    } finally {
      setBusy(false)
    }
  }
  return (
    <div class="my-3 border-t border-edge pt-3" data-remember-lan>
      <div class="flex items-center justify-between gap-3">
        <span>{t('settings.remember_lan')}</span>
        <Switch
          on={Boolean(saved)}
          label={t('settings.remember_lan')}
          onClick={() => {
            if (saved) forgetLanCode()
            else {
              setCode('')
              setError('')
              setOpen(true)
            }
          }}
        />
      </div>
      <p class="mt-1 text-xs text-muted">{t('settings.remember_hint')}</p>
      {saved && (
        <Button class="mt-2" onClick={forgetLanCode}>
          {t('settings.forget_lan')}
        </Button>
      )}
      {open && (
        <Dialog label={t('settings.remember_lan')} onClose={close} locked={busy}>
          <DialogTitle class="mb-2">{t('settings.remember_lan')}</DialogTitle>
          <p class="mb-3 text-muted">{t('settings.remember_hint')}</p>
          <label htmlFor="remember-lan-code">{t('settings.lan_access_code')}</label>
          <PasswordInput
            id="remember-lan-code"
            class="mt-1"
            value={code}
            disabled={busy}
            autoComplete="off"
            onInput={e => setCode(e.currentTarget.value)}
          />
          {error && (
            <p role="alert" class="mt-2 text-red">
              {error}
            </p>
          )}
          <div class="mt-3 flex flex-wrap gap-2">
            <Button disabled={busy || !code.trim()} onClick={() => void save()}>
              {t('settings.remember_save')}
            </Button>
            <Button disabled={busy} onClick={close}>
              {t('common.cancel')}
            </Button>
          </div>
        </Dialog>
      )}
    </div>
  )
}
