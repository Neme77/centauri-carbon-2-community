import { useCallback, useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { ask } from '@/lib/confirm'
import { errText, post, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { printer } from '@/lib/state'

type ToolStatus = {
  busy: boolean
  generation: number
  error: string
  mode: number
  active: number | null
  original: number | null
  configured: number | null
  saved: number | null
  editable: boolean
  pending_restart: boolean
  pending_conflict: boolean
  cell?: {
    stage: number
    owned: boolean
    max_grams: number
    counts_per_gram: number
    tare_counts: number | null
    grams: number
  }
  reports: string[]
}
const ToolProgress = ({ refresh }: { refresh: () => Promise<void> }) => {
  usePoll(refresh, 500)
  return null
}
export const CalibrationTools = ({ panel = 'both' }: { panel?: 'both' | 'cell' | 'rotation' }) => {
  const link = printer.use(),
    d = link.data
  const [data, setData] = useState<ToolStatus | null>(null)
  const [error, setError] = useState('')
  const [sending, setSending] = useState(false)
  const sendingRef = useRef(false)
  const [basis, setBasis] = useState<number | null>(null)
  const [requested, setRequested] = useState('100'),
    [measured, setMeasured] = useState('')
  const [grams, setGrams] = useState('')
  const guideHydrated = useRef(false)
  useEffect(() => {
    if (!data) return
    if (!guideHydrated.current) {
      guideHydrated.current = true
      if (data.cell?.stage === 3) setGrams(String(data.cell.grams))
    }
    if (data.cell?.stage === 1) setGrams('')
  }, [data?.cell?.stage])
  useEffect(() => {
    if (!data || data.busy || data.error !== 'none') return
    if (data.mode === 1 && data.active != null) {
      setBasis(data.active)
      setMeasured('')
    } else if (data.mode === 2) setMeasured('')
  }, [data?.generation, data?.busy, data?.active, data?.error, data?.mode])
  const refresh = useCallback(async () => {
    try {
      setData(await request('/api/calibration-tools/status'))
      setError('')
    } catch (e) {
      setError(errText(e))
    }
  }, [])
  useEffect(() => {
    void refresh()
  }, [refresh])
  const native = d?.print?.native?.state
  const idle =
    link.ok &&
    d?.connected &&
    d.machine?.status === 1 &&
    d.last_message_age >= 0 &&
    d.last_message_age <= 15 &&
    (native === 'standby' || native === 'complete') &&
    !d.bed_drying?.active
  const busy = sending || data?.busy
  const current = data?.active
  const amount = Number(requested),
    actual = Number(measured)
  const calculated = basis != null && amount > 0 && actual > 0 ? (basis * actual) / amount : NaN
  const valid = Number.isFinite(calculated) && calculated >= 0.01 && calculated <= 1000
  const send = async (action: string, confirmation?: string) => {
    if (sendingRef.current || busy || !idle) return
    sendingRef.current = true
    try {
      if (confirmation && !(await ask(confirmation))) return
      setSending(true)
      setError('')
      await post('/api/calibration-tools/action', action)
      await refresh()
    } catch (e) {
      setError(errText(e))
    } finally {
      sendingRef.current = false
      setSending(false)
    }
  }
  const number = (value: number | null | undefined) => (value == null ? '—' : `${Number(value.toFixed(6))} mm`)
  const pending = data?.pending_restart
  const cell = data?.cell
  const stage = cell?.stage || 0
  const cold =
    idle &&
    d?.extruder?.target === 0 &&
    d?.heater_bed?.target === 0 &&
    d.extruder.temperature <= 50 &&
    d.heater_bed.temperature <= 50
  const mass = Number(grams)
  const validMass = Number.isFinite(mass) && mass >= 50 && mass <= (cell?.max_grams || 0)
  const canApply = idle && !busy && !pending && !stage && current != null
  const restoreTarget = data?.configured
  const content = (
    <>
      {panel !== 'rotation' && (
        <CardHead icon="control" title={panel === 'cell' ? 'caltools.cell' : 'caltools.title'} />
      )}
      {data?.busy && <ToolProgress refresh={refresh} />}
      <div class={panel === 'both' ? 'grid items-start gap-3.5 cc2-lg:grid-cols-2' : ''}>
        {panel !== 'rotation' && (
          <div class="space-y-3">
            {panel === 'both' && <h3 class="text-base font-semibold">{t('caltools.cell')}</h3>}
            <p class="text-xs text-muted">{t('caltools.cell_hint')}</p>
            <div class="grid grid-cols-2 gap-2">
              <Button disabled={!idle || busy || !!stage} onClick={() => send('cell:read')}>
                {t('caltools.read_cell')}
              </Button>
              <Button disabled={!idle || busy || !!stage} onClick={() => send('cell:diagnostic')}>
                {t('caltools.diagnose')}
              </Button>
            </div>
            <div class="grid grid-cols-2 gap-2">
              <Button
                disabled={!cold || busy || !!stage || pending}
                onClick={() => send('cell:tare', t('cellguide.tare_confirm'))}
              >
                {t('cellguide.tare')}
              </Button>
              <Button
                disabled={!cold || busy || !!stage || pending}
                onClick={() => send('cell:start', t('cellguide.start_confirm'))}
              >
                {t('cellguide.start')}
              </Button>
            </div>
            {!!stage && (
              <fieldset
                class="cc2-cell-guide space-y-3 rounded-md border border-edge bg-field p-3"
                aria-label={t('cellguide.start')}
              >
                <p class="text-xs" role="status">
                  {t(
                    stage === 1
                      ? 'cellguide.zero_hint'
                      : stage === 2
                        ? 'cellguide.load_hint'
                        : stage === 3
                          ? 'cellguide.review_hint'
                          : stage === 4
                            ? 'cellguide.applied_hint'
                            : stage === 5
                              ? 'cellguide.saved_hint'
                              : 'cellguide.unknown_hint'
                  )}
                </p>
                {stage >= 2 && stage <= 3 && (
                  <label class="block text-xs">
                    {tpl('cellguide.grams', { max: String(cell?.max_grams || 0) })}
                    <Input
                      type="number"
                      min="50"
                      max={cell?.max_grams || 0}
                      step="0.01"
                      value={grams}
                      disabled={!!busy}
                      onInput={e => setGrams(e.currentTarget.value)}
                    />
                  </label>
                )}
                {stage >= 3 && stage <= 5 && (
                  <p class="text-xs">{tpl('cellguide.used_mass', { value: String(cell?.grams) })}</p>
                )}
                {stage >= 3 && stage <= 5 && (
                  <p class="text-xs">
                    {t('cellguide.result')}: {cell?.counts_per_gram} / {cell?.tare_counts ?? '—'}
                  </p>
                )}
                <div class="grid grid-cols-2 gap-2">
                  {stage >= 1 && stage <= 3 && (
                    <Button disabled={!cold || busy} onClick={() => send('cell:zero', t('cellguide.tare_confirm'))}>
                      {t('cellguide.tare')}
                    </Button>
                  )}
                  {stage >= 2 && stage <= 3 && (
                    <Button
                      disabled={!cold || busy || !validMass}
                      onClick={() =>
                        send(`cell:measure:${mass}`, tpl('cellguide.measure_confirm', { value: String(mass) }))
                      }
                    >
                      {t('cellguide.measure')}
                    </Button>
                  )}
                  {stage === 3 && (
                    <Button
                      disabled={!cold || busy || mass !== cell?.grams}
                      onClick={() => send('cell:accept', t('cellguide.accept_confirm'))}
                    >
                      {t('cellguide.accept')}
                    </Button>
                  )}
                  {(stage <= 3 || stage === 6) && (
                    <Button disabled={!idle || busy || !cell?.owned} onClick={() => send('cell:abort')}>
                      {t('cellguide.abort')}
                    </Button>
                  )}
                  {stage === 4 && (
                    <>
                      <Button disabled={!cold || busy} onClick={() => send('cell:save', t('cellguide.save_confirm'))}>
                        {t('caltools.save')}
                      </Button>
                      <Button
                        disabled={!cold || busy}
                        onClick={() => send('cell:discard', t('cellguide.discard_confirm'))}
                      >
                        {t('cellguide.discard')}
                      </Button>
                    </>
                  )}
                  {stage === 5 && (
                    <Button
                      disabled={!cold || busy}
                      onClick={() => send('cell:restart', t('cellguide.restart_confirm'))}
                    >
                      {t('caltools.restart')}
                    </Button>
                  )}
                </div>
              </fieldset>
            )}
            <p class="text-xs text-muted">{t('caltools.no_automatic_motion')}</p>
          </div>
        )}
        {panel !== 'cell' && (
          <div class="cc2-rotation space-y-2">
            <h3 class="text-base font-semibold">Rotation Distance</h3>
            <dl class="grid grid-cols-3 gap-2 text-xs">
              <div>
                <dt class="text-muted">{t('caltools.configured')}</dt>
                <dd>{number(data?.configured)}</dd>
              </div>
              <div>
                <dt class="text-muted">{t('caltools.saved')}</dt>
                <dd>{number(data?.saved)}</dd>
              </div>
              <div>
                <dt class="text-muted">{t('caltools.active')}</dt>
                <dd>{number(current)}</dd>
              </div>
            </dl>
            <div class="grid grid-cols-2 gap-2">
              <Button disabled={!idle || busy || !!stage} onClick={() => send('rotation:read')}>
                {t('caltools.read_rotation')}
              </Button>

              <Button
                disabled={!canApply || restoreTarget == null}
                onClick={() =>
                  send(
                    `rotation:apply:${current}:${restoreTarget}`,
                    tpl('caltools.apply_confirm', { value: number(restoreTarget) })
                  )
                }
              >
                {t('caltools.restore')}
              </Button>
              <Button
                disabled={!idle || busy || !!stage || current == null || !data?.editable}
                onClick={() => send(`rotation:save:${current}`, t('caltools.save_confirm'))}
              >
                {t('caltools.save')}
              </Button>
              <Button
                disabled={
                  !idle ||
                  busy ||
                  !pending ||
                  !!stage ||
                  data?.pending_conflict ||
                  d?.extruder?.target !== 0 ||
                  d?.heater_bed?.target !== 0
                }
                onClick={() => send('rotation:restart', t('caltools.restart_confirm'))}
              >
                {t('caltools.restart')}
              </Button>
            </div>
            <div class="cc2-rotation-calculator space-y-2 border-t border-edge pt-2">
              <h4 class="text-xs font-medium text-cyan">{t('caltools.calculator')}</h4>
              <p class="my-2 text-xs text-muted">{t('caltools.rotation_hint')}</p>
              <div class="grid grid-cols-2 gap-2">
                <label class="text-xs">
                  {t('caltools.requested')}
                  <Input
                    type="number"
                    min="0.01"
                    step="0.01"
                    value={requested}
                    onInput={e => setRequested(e.currentTarget.value)}
                  />
                </label>
                <label class="text-xs">
                  {t('caltools.measured')}
                  <Input
                    type="number"
                    min="0.01"
                    step="0.01"
                    value={measured}
                    onInput={e => setMeasured(e.currentTarget.value)}
                  />
                </label>
              </div>
              <p class="text-xs">
                {t('caltools.calculated')}: <strong>{valid ? number(calculated) : '—'}</strong>
              </p>
              <Button
                disabled={!canApply || !valid || basis !== current}
                onClick={() =>
                  send(
                    `rotation:apply:${current}:${calculated.toFixed(10)}`,
                    tpl('caltools.apply_confirm', { value: number(calculated) })
                  )
                }
              >
                {t('caltools.apply')}
              </Button>
            </div>
            {pending && (
              <p class="text-xs text-amber">{t(data?.pending_conflict ? 'caltools.conflict' : 'caltools.pending')}</p>
            )}
          </div>
        )}
      </div>
      <p role="status" aria-live="polite" class="mt-2 text-xs">
        {error
          ? tpl('common.rejected_error', { error })
          : data?.error && data.error !== 'none'
            ? tpl('common.rejected_error', { error: data.error })
            : busy
              ? t('caltools.running')
              : !idle
                ? t('caltools.idle_required')
                : data?.generation
                  ? t('caltools.finished')
                  : ''}
      </p>
      {!!data?.reports?.length && (
        <pre class="max-h-64 overflow-auto whitespace-pre-wrap break-words rounded-md border border-edge bg-field p-3 text-xs">
          {data.reports.join('\n')}
        </pre>
      )}
    </>
  )
  return panel === 'rotation' ? (
    <div class="cc2-native-tools cc2-rotation-tools mt-3 border-t border-edge pt-3">{content}</div>
  ) : (
    <Card class="cc2-native-tools cc2-cell-tools">{content}</Card>
  )
}
