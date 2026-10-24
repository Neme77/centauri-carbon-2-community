import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { PasswordInput } from '@/components/ui/field'
import { ask } from '@/lib/confirm'
import { errText, notify, request } from '@/lib/api'
import { experimental, refreshExperiments, type ExperimentState } from '@/lib/experiments'
import { t } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { openPage } from '@/lib/state'

export const Experiments = () => {
  usePoll(refreshExperiments, 5000)
  const { data, ok } = experimental.use()
  const [code, setCode] = useState(''),
    [busy, setBusy] = useState(false),
    [error, setError] = useState('')
  const mounted = useRef(true)
  useEffect(() => {
    mounted.current = true
    return () => {
      mounted.current = false
    }
  }, [])
  const change = async (feature: 'canvas-uds' | 'quiet-motors') => {
    if (!data || !ok || busy) return
    const enabled = feature === 'canvas-uds' ? data.canvas_uds : data.quiet_motors
    const message =
      feature === 'canvas-uds'
        ? enabled
          ? 'experiments.canvas_off_confirm'
          : 'experiments.canvas_on_confirm'
        : enabled
          ? 'experiments.quiet_off_confirm'
          : 'experiments.quiet_on_confirm'
    if (!(await ask(t(message))) || !mounted.current) return
    setBusy(true)
    setError('')
    try {
      const updated: ExperimentState = await request('/api/experiments', {
        method: 'POST',
        headers: { 'X-CC2-LAN': code.trim() },
        body: `${feature}:${enabled ? 'off' : 'on'}`,
      })
      if (!mounted.current) return
      experimental.set({ data: updated, ok: true })
      setCode('')
      notify(t('experiments.saved'))
    } catch (e) {
      if (mounted.current) setError(errText(e))
    } finally {
      if (mounted.current) setBusy(false)
    }
  }
  const disabled = busy || !ok || !data?.editable || !data.ready || !code.trim()
  return (
    <Card class="mt-3.5" data-experiments>
      <CardHead icon="settings" title="experiments.title" />
      <p class="text-muted">{t('experiments.intro')}</p>
      <p class="mt-2 text-muted">{t('experiments.guard')}</p>
      {ok && data?.quiet_motors && !data.normal_confirmed && (
        <div class="mt-3 rounded-md border border-amber/50 bg-amber/10 p-3" data-experiment-recovery role="status">
          <p class="text-amber">{t('experiments.normal_required')}</p>
          <Button class="mt-2" onClick={() => openPage('control')}>
            {t('common.control')}
          </Button>
        </div>
      )}
      <label class="my-3 block max-w-md" htmlFor="experiments-lan">
        {t('experiments.lan_code')}
        <PasswordInput
          id="experiments-lan"
          class="mt-1"
          value={code}
          disabled={busy}
          onInput={e => setCode(e.currentTarget.value)}
          autoComplete="off"
        />
      </label>
      <div class="grid gap-3.5 cc2-md:grid-cols-2">
        <div class="rounded-md border border-edge p-3">
          <h3 class="text-base font-semibold">{t('experiments.canvas_title')}</h3>
          <p class="my-2 text-muted">{t('experiments.canvas_hint')}</p>
          <Button
            role="switch"
            aria-checked={Boolean(data?.canvas_uds)}
            aria-label={t('experiments.canvas_title')}
            disabled={disabled || data?.cli_canvas}
            onClick={() => void change('canvas-uds')}
          >
            {t(data?.canvas_uds ? 'experiments.disable' : 'experiments.enable')}
          </Button>
          <p class="mt-2 text-xs text-muted" data-experiment-canvas-source>
            {t(
              !ok
                ? 'experiments.unavailable'
                : data?.canvas_uds
                  ? data.canvas_uds_available
                    ? 'experiments.using_uds'
                    : 'experiments.fallback'
                  : 'experiments.using_mqtt'
            )}
          </p>
        </div>
        <div class="rounded-md border border-edge p-3">
          <h3 class="text-base font-semibold">{t('experiments.quiet_title')}</h3>
          <p class="my-2 text-muted">{t('experiments.quiet_hint')}</p>
          <Button
            role="switch"
            aria-checked={Boolean(data?.quiet_motors)}
            aria-label={t('experiments.quiet_title')}
            disabled={disabled || (data?.quiet_motors && !data.normal_confirmed)}
            onClick={() => void change('quiet-motors')}
          >
            {t(data?.quiet_motors ? 'experiments.disable' : 'experiments.enable')}
          </Button>
        </div>
      </div>
      {!ok && <p class="mt-3 text-amber">{t('experiments.unavailable')}</p>}
      {data && !data.editable && <p class="mt-3 text-amber">{t('experiments.invalid')}</p>}
      {error && (
        <p class="mt-3 break-words text-red" role="alert">
          {error}
        </p>
      )}
    </Card>
  )
}
