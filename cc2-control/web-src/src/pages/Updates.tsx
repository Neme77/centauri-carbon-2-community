import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { PasswordInput, Select } from '@/components/ui/field'
import { ask } from '@/lib/confirm'
import { errText, notify, request } from '@/lib/api'
import { i18n, t } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import {
  checkUpdates,
  compatible,
  newer,
  refresh,
  releaseNotes,
  setUpdateChannel,
  status,
  updateCatalog,
  updateChannel,
} from '@/lib/updates'

export const Updates = () => {
  usePoll(refresh, 2000)
  const state = status.use().data
  const language = i18n.use().lang
  const { value: channel } = updateChannel.use()
  const { index, checking } = updateCatalog.use()
  const [working, setWorking] = useState(false),
    [code, setCode] = useState(''),
    [error, setError] = useState('')
  const [file, setFile] = useState<File | null>(null)
  const mounted = useRef(true),
    selection = useRef(0)
  const release = compatible(index, state?.firmware || '', channel)[0]
  const available = release && newer(release.version, state?.installed || '')
  const notes = available ? releaseNotes(release, language) : null
  const busy = working || Boolean(state?.busy)
  const runCheck = async (force = true) => {
    setError('')
    try {
      await checkUpdates(force)
    } catch (e) {
      if (mounted.current) setError(errText(e))
    }
  }

  useEffect(() => {
    mounted.current = true
    void runCheck(false)
    return () => {
      mounted.current = false
    }
  }, [])
  const changeChannel = async (next: string) => {
    if (next === 'canvas-uds' && !(await ask(t('updates.experimental_warning')))) return
    selection.current++
    setUpdateChannel(next)
    setFile(null)
  }
  const prepare = async (local: boolean) => {
    if (!state?.ready || !code.trim() || busy || (local ? !file : !available)) return
    if (!(await ask(t('updates.prepare_confirm')))) return
    const chosen = selection.current
    setWorking(true)
    setError('')
    try {
      if (chosen !== selection.current) return
      await request(local ? '/api/app-update/upload' : '/api/app-update/download', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream', 'X-CC2-LAN': code.trim() },
        body: local ? file : release.url,
        signal: AbortSignal.timeout(300000),
      })
      await refresh()
      notify(t('updates.verified'))
    } catch (e) {
      setError(errText(e))
    } finally {
      if (mounted.current) setWorking(false)
    }
  }
  const install = async () => {
    if (
      !state?.ready ||
      state.phase !== 'ready' ||
      !code.trim() ||
      busy ||
      !(await ask(t('updates.install_confirm'), true))
    )
      return
    setWorking(true)
    setError('')
    try {
      await request('/api/app-update/install', {
        method: 'POST',
        headers: { 'X-CC2-LAN': code.trim() },
        body: state.job,
      })
      setCode('')
      notify(t('updates.restarting'))
      await refresh()
    } catch (e) {
      setError(errText(e))
    } finally {
      if (mounted.current) setWorking(false)
    }
  }
  return (
    <Card>
      <CardHead icon="settings" title="updates.title" sub="updates.application_only" />
      <div class="grid gap-4 cc2-sm:grid-cols-2">
        <div>
          <span class="text-muted">{t('updates.installed')}</span>
          <b class="mt-1 block">{state?.installed || '—'}</b>
        </div>
        <label>
          {t('updates.channel')}
          <Select
            aria-label={t('updates.channel')}
            class="mt-1"
            value={channel}
            disabled={busy}
            onChange={e => {
              const next = e.currentTarget.value
              e.currentTarget.value = channel
              void changeChannel(next)
            }}
          >
            <option value="stable">{t('updates.stable')}</option>
            <option value="canvas-uds">{t('updates.experimental')}</option>
          </Select>
        </label>
      </div>
      <div class="my-4 flex flex-wrap items-center gap-3">
        <Button disabled={checking || busy || !state?.enabled} onClick={() => void runCheck()}>
          {t('updates.check')}
        </Button>
        <span>
          {checking
            ? t('updates.checking')
            : available
              ? `${t('updates.available')}: ${release.version}`
              : t('updates.no_update')}
        </span>
      </div>
      {notes?.text && (
        <div class="my-3 text-muted">
          {notes.language !== language && <p class="mb-1 text-sm">{t('updates.notes_in_english')}</p>}
          <p class="whitespace-pre-wrap break-words" lang={notes.language}>
            {notes.text}
          </p>
        </div>
      )}
      {!state?.enabled && <p class="my-3 text-amber">{t('updates.bootstrap')}</p>}
      <p class="my-3 text-sm text-muted">{t('updates.requirements')}</p>
      <label class="block max-w-md" htmlFor="update-lan-code">
        {t('settings.lan_access_code')}
        <PasswordInput
          id="update-lan-code"
          class="mt-1"
          value={code}
          disabled={busy}
          onInput={e => setCode(e.currentTarget.value)}
          autoComplete="off"
        />
      </label>
      <div class="my-4 flex flex-wrap gap-3">
        <Button disabled={!available || !state?.ready || !code.trim() || busy} onClick={() => void prepare(false)}>
          {t('updates.download')}
        </Button>
        <Button
          disabled={state?.phase !== 'ready' || !state.ready || !code.trim() || busy}
          onClick={() => void install()}
        >
          {t('updates.install')}
        </Button>
      </div>
      <details class="border-t border-edge pt-3">
        <summary class="cursor-pointer">{t('updates.local_file')}</summary>
        <input
          class="my-3 block max-w-full"
          type="file"
          accept=".cc2update"
          disabled={busy}
          onChange={e => {
            const selected = e.currentTarget.files?.[0] || null
            selection.current++
            setFile(selected && selected.size >= 1108 && selected.size <= 33555540 ? selected : null)
          }}
        />
        <Button disabled={!file || !state?.ready || !code.trim() || busy} onClick={() => void prepare(true)}>
          {t('updates.verify_file')}
        </Button>
      </details>
      <p class="mt-4">
        {t('updates.status')}{' '}
        {t(
          state?.phase === 'complete'
            ? 'updates.complete'
            : state?.phase === 'failed' || state?.phase === 'interrupted'
              ? 'updates.failed'
              : state?.phase === 'ready'
                ? 'updates.verified'
                : busy
                  ? 'updates.working'
                  : 'updates.idle'
        )}
      </p>
      {state?.version && (
        <p>
          {t('updates.selected')} {state.version}
        </p>
      )}
      {state?.phase === 'complete' && (
        <Button class="mt-3" onClick={() => location.reload()}>
          {t('updates.reload')}
        </Button>
      )}
      {error && (
        <p class="mt-3 break-words text-red" role="alert">
          {error}
        </p>
      )}
    </Card>
  )
}
