import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input, PasswordInput } from '@/components/ui/field'
import { ask } from '@/lib/confirm'
import { errText, notify, request } from '@/lib/api'
import { loadUiPreferences, t, tpl, type Key } from '@/lib/i18n'
import { loadPresets, refreshPrinter } from '@/lib/state'

type Preview = {
  token: string
  version: string
  created: number
  sections: number
  plates: number
  meshes: number
  nozzles: number
  spools: number
  events: number
}
const CATEGORIES: Key[] = ['backup.interface', 'backup.materials', 'backup.plates', 'backup.spools']
const MAXIMUM = 512 * 1024

export const Backup = () => {
  const [code, setCode] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState(''),
    [progress, setProgress] = useState(0),
    [preview, setPreview] = useState<Preview | null>(null),
    [mask, setMask] = useState(0)
  const mounted = useRef(true),
    token = useRef(''),
    lan = useRef('')
  lan.current = code.trim()
  const call = (operation: string, body: BodyInit = '', extra: Record<string, string> = {}) =>
    request(`/api/backup/import/${operation}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-CC2-LAN': lan.current,
        ...(token.current ? { 'X-CC2-Backup': token.current } : {}),
        ...extra,
      },
      body,
    })
  useEffect(() => {
    mounted.current = true
    return () => {
      mounted.current = false
      // A bounded server lease also discards previews after an abrupt disconnect.
      if (token.current) void call('cancel').catch(() => {})
    }
  }, [])
  const download = async () => {
    setProgress(-1)
    setBusy(true)
    setError('')
    try {
      const text: unknown = await request('/api/backup/export')
      if (typeof text !== 'string' || text.length > MAXIMUM) throw Error(t('backup.invalid_file'))
      // Preserve the exact UTF-8 text: re-stringifying parsed JSON would change its CRC.
      const blob = new Blob([text], { type: 'application/json;charset=utf-8' })
      const url = URL.createObjectURL(blob),
        link = document.createElement('a')
      link.href = url
      link.download = `CC2-Control-backup-${new Date().toLocaleDateString('sv-SE')}.json`
      document.body.appendChild(link)
      link.click()
      link.remove()
      window.setTimeout(() => URL.revokeObjectURL(url), 1000)
      notify(t('backup.download_started'))
    } catch (e) {
      if (mounted.current) setError(errText(e))
    } finally {
      if (mounted.current) setBusy(false)
    }
  }
  const cancel = async () => {
    setBusy(true)
    setError('')
    try {
      if (token.current) await call('cancel')
    } catch (e) {
      if (mounted.current) setError(errText(e))
    } finally {
      token.current = ''
      if (mounted.current) {
        setPreview(null)
        setMask(0)
        setProgress(0)
        setBusy(false)
      }
    }
  }
  const upload = async (file: File | undefined) => {
    if (!file) return
    if (!lan.current) return notify(t('backup.enter_lan'), 'error')
    if (file.size < 20 || file.size > MAXIMUM) return notify(t('backup.invalid_file'), 'error')
    setBusy(true)
    setError('')
    setPreview(null)
    setProgress(0)
    try {
      if (token.current) await call('cancel')
      token.current = ''
      const bytes = new Uint8Array(await file.arrayBuffer())
      const started = await call('start', String(bytes.length))
      token.current = started.token
      for (let offset = 0; offset < bytes.length; offset += 8192) {
        if (!mounted.current) throw Error(t('backup.cancelled'))
        const chunk = bytes.slice(offset, Math.min(offset + 8192, bytes.length))
        await call('chunk', chunk, { 'X-CC2-Offset': String(offset) })
        if (mounted.current) setProgress(Math.round(((offset + chunk.length) / bytes.length) * 100))
      }
      const result: Preview = await call('finish')
      if (mounted.current) {
        setPreview(result)
        setMask(result.sections)
      }
    } catch (e) {
      if (token.current) await call('cancel').catch(() => {})
      token.current = ''
      if (mounted.current) setError(errText(e))
    } finally {
      if (mounted.current) setBusy(false)
    }
  }
  const restore = async () => {
    if (!preview || !mask || !lan.current || busy) return
    if (!(await ask(t('backup.confirm_replace'), true)) || !mounted.current) return
    setProgress(-1)
    setBusy(true)
    setError('')
    try {
      await call('restore', String(mask))
      token.current = ''
      setPreview(null)
      setMask(0)
      setCode('')
      await Promise.all([loadUiPreferences(), loadPresets(), refreshPrinter()])
      notify(t('backup.restored'))
    } catch (e) {
      if (mounted.current) setError(errText(e))
    } finally {
      if (mounted.current) setBusy(false)
    }
  }
  return (
    <div class="grid gap-3.5">
      <Card>
        <CardHead icon="settings" title="backup.title" sub="backup.description" />
        <p class="mb-3 text-muted">{t('backup.coverage')}</p>
        <p class="mb-3 text-sm text-muted">{t('backup.exclusions')}</p>
        <p class="mb-3 text-sm text-muted">{t('backup.integrity_hint')}</p>
        <Button disabled={busy} onClick={download}>
          {t('backup.download')}
        </Button>
      </Card>
      <Card>
        <CardHead icon="settings" title="backup.restore" sub="backup.restore_hint" />
        <p class="mb-3 rounded border border-amber/40 bg-amber/10 p-3 text-amber">{t('backup.physical_warning')}</p>
        <label class="mb-3 block max-w-md" htmlFor="backup-lan">
          <span class="mb-1 block font-semibold">{t('settings.lan_access_code')}</span>
          <PasswordInput id="backup-lan" value={code} disabled={busy} onInput={e => setCode(e.currentTarget.value)} />
        </label>
        <label class="mb-3 block" htmlFor="backup-file">
          <span class="mb-1 block font-semibold">{t('backup.choose_file')}</span>
          <Input
            id="backup-file"
            type="file"
            accept=".json,.cc2backup"
            disabled={busy || !code.trim()}
            onChange={e => {
              const file = e.currentTarget.files?.[0]
              e.currentTarget.value = ''
              void upload(file)
            }}
          />
        </label>
        {busy && (
          <p role="status" class="my-2 text-muted">
            {progress < 0 ? t('backup.working') : tpl('backup.progress', { percent: progress })}
          </p>
        )}
        {preview && (
          <div class="my-3 rounded border border-edge p-3">
            <h3 class="mb-2 text-base font-semibold">{t('backup.preview')}</h3>
            <p class="break-words text-sm text-muted">
              {tpl('backup.source', {
                version: preview.version,
                date: new Date(preview.created * 1000).toLocaleString(),
              })}
            </p>
            <p class="my-2 text-sm">
              {tpl('backup.counts', {
                plates: preview.plates,
                meshes: preview.meshes,
                nozzles: preview.nozzles,
                spools: preview.spools,
                events: preview.events,
              })}
            </p>
            <p class="mb-2 text-sm text-muted">{t('backup.preview_hint')}</p>
            <div class="grid gap-2 cc2-md:grid-cols-2">
              {CATEGORIES.map((key, i) => (
                <label class="flex items-center gap-2" key={key}>
                  <input
                    type="checkbox"
                    disabled={busy || !(preview.sections & (1 << i))}
                    checked={Boolean(mask & (1 << i))}
                    onChange={e => setMask(e.currentTarget.checked ? mask | (1 << i) : mask & ~(1 << i))}
                  />
                  {t(key)}
                  {!(preview.sections & (1 << i)) && <small class="text-muted">{t('backup.absent')}</small>}
                </label>
              ))}
            </div>
            <div class="mt-3 flex flex-wrap gap-2">
              <Button variant="danger" disabled={busy || !mask || !code.trim()} onClick={restore}>
                {t('backup.restore_selected')}
              </Button>
              <Button disabled={busy} onClick={cancel}>
                {t('common.cancel')}
              </Button>
            </div>
          </div>
        )}
        {error && (
          <p role="alert" class="mt-3 whitespace-pre-wrap break-words text-red">
            {error}
          </p>
        )}
      </Card>
    </div>
  )
}
