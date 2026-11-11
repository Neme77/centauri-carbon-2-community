import { useCallback, useRef, useState } from 'preact/hooks'
import { createPortal } from 'preact/compat'
import { ChevronDown, Radio, ScanLine } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Select } from '@/components/ui/field'
import { request, post, errText, notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { rfidLifecycle } from '@/lib/rfid-lifecycle'
import { canvas, canvasHex, refreshCanvas } from '@/lib/canvas'

type Preview = {
  uid: string
  material: string
  color: string
  brand: string
  sku: string
  diameter: number
  nozzle_min: number
  nozzle_max: number
  bed_min: number
  bed_max: number
}
type Status = {
  installed: boolean
  ready: boolean
  busy: boolean
  armed: boolean
  state: string
  reason?: string
  error?: string
  generation: number
  preview: Preview | null
}
export const RfidScan = ({ idle, toggleTarget }: { idle: boolean; toggleTarget: HTMLElement | null }) => {
  const [status, setStatus] = useState<Status | null>(null)
  const { data: lifecycle } = rfidLifecycle.use()
  const [slot, setSlot] = useState('')
  const [sending, setSending] = useState(false)
  const [expanded, setExpanded] = useState(false)
  const [assigned, setAssigned] = useState<{ slot: number; material: string; color: string } | null>(null)
  const nativeCanvas = canvas.use()
  const lock = useRef(false)
  const refresh = useCallback(async () => {
    try {
      setStatus(await request('/api/rfid'))
    } catch {
      setStatus(null)
    }
  }, [])
  usePoll(refresh, status?.busy ? 700 : 3000)
  const action = async (command: string) => {
    if (lock.current) return
    lock.current = true
    setSending(true)
    try {
      await post('/api/rfid/action', command)
      await refresh()
      return true
    } catch (e) {
      notify(errText(e), 'error')
      await refresh()
      return false
    } finally {
      lock.current = false
      setSending(false)
    }
  }
  if (!status?.installed && !lifecycle?.installed) return null
  if (!status) return null
  const preview = status.preview
  const canApply = idle && status.ready && !status.busy && Boolean(preview) && /^[0-3]$/.test(slot)
  const apply = async () => {
    if (!canApply || sending) return
    const generation = status.generation
    const chosen = Number(slot)
    const captured = preview
    if (!(await ask(tpl('rfid.confirm', { slot: chosen + 1, material: preview?.material || '' })))) return
    if (await action(`apply:${generation}:${chosen}`)) {
      if (captured) setAssigned({ slot: chosen, material: captured.material, color: captured.color })
      notify(t('rfid.submitted'))
      setSlot('')
      await refreshCanvas()
    }
  }
  const tray = assigned && nativeCanvas.model?.connected ? nativeCanvas.model.trays[assigned.slot] : null
  const reported = Boolean(
    assigned &&
      tray &&
      (tray.filament_type === assigned.material || tray.filament_name === assigned.material) &&
      tray.filament_color &&
      canvasHex(tray.filament_color, assigned.slot) === assigned.color.toUpperCase()
  )
  const present = tray ? Number(tray.status) > 0 : null
  const toggle = (
    <div class="flex items-center gap-2">
      {!expanded && (status.busy || preview || assigned) && (
        <span class="text-xs text-cyan">
          {t(status.busy ? 'rfid.scanning' : assigned ? 'rfid.read_complete' : 'rfid.captured')}
        </span>
      )}
      <Button
        data-rfid-toggle
        title={t('rfid.title')}
        aria-expanded={expanded}
        aria-controls="cc2-rfid-panel"
        onClick={() => setExpanded(value => !value)}
      >
        <ScanLine size={16} /> {t('rfid.short_title')}
        <ChevronDown size={16} class={expanded ? 'rotate-180' : ''} />
      </Button>
    </div>
  )
  return (
    <div class={expanded ? 'mt-3.5 border-t border-edge pt-3.5' : undefined} data-rfid-scan>
      {toggleTarget && createPortal(toggle, toggleTarget)}
      <div id="cc2-rfid-panel" hidden={!expanded}>
        <h3 class="mb-2 text-base font-semibold">{t('rfid.title')}</h3>
        <p class="text-xs text-muted">{t('rfid.hint')}</p>
        {!status.ready && (
          <p role="status" class="mt-2 text-xs text-amber" data-rfid-blocked>
            {t(
              status.reason === 'nozzle_hot'
                ? 'rfid.nozzle_hot'
                : status.reason === 'bed_hot'
                  ? 'rfid.bed_hot'
                  : status.reason === 'heaters_on'
                    ? 'rfid.heaters_on'
                    : status.reason === 'stale_telemetry'
                      ? 'rfid.wait_fresh'
                      : 'rfid.not_ready'
            )}
          </p>
        )}
        <div class="mt-3.5 flex flex-wrap gap-2">
          <Button
            data-rfid-start
            disabled={!idle || !status.ready || status.busy || sending}
            onClick={() => {
              setSlot('')
              setAssigned(null)
              void action('scan')
            }}
          >
            <ScanLine size={16} /> {t('rfid.scan')}
          </Button>
          {(status.busy || preview) && (
            <Button data-rfid-cancel disabled={sending} onClick={() => void action('cancel')}>
              {t('common.cancel')}
            </Button>
          )}
        </div>
        <p role="status" class="mt-2 text-sm" data-rfid-state>
          {status.busy ? (
            <span class="inline-flex items-center gap-2">
              <Radio size={16} />{' '}
              {t(
                status.state === 'cancelled' || status.state === 'state_changed'
                  ? 'rfid.cancelling'
                  : status.armed
                    ? 'rfid.armed'
                    : 'rfid.preparing'
              )}
            </span>
          ) : (
            t(
              preview
                ? 'rfid.preview_ready'
                : status.state === 'submitted'
                  ? 'rfid.submitted'
                  : status.state === 'idle' || status.state === 'cancelled'
                    ? 'rfid.idle'
                    : 'rfid.failed'
            )
          )}
        </p>
        {assigned && (
          <div class="mt-3.5 rounded-md border border-edge p-3 text-sm" data-rfid-assignment>
            <p class="font-semibold">{tpl('rfid.assignment_title', { slot: assigned.slot + 1 })}</p>
            <dl class="mt-2 grid grid-cols-2 gap-x-4 gap-y-1">
              <dt>{t('rfid.metadata')}</dt>
              <dd data-rfid-metadata>{t(reported ? 'rfid.metadata_reported' : 'rfid.metadata_pending')}</dd>
              <dt>{t('rfid.presence')}</dt>
              <dd data-rfid-presence>
                {t(present === null ? 'rfid.presence_unknown' : present ? 'rfid.present' : 'rfid.absent')}
              </dd>
            </dl>
            <p class="mt-2 text-xs text-muted">{t('rfid.presence_hint')}</p>
          </div>
        )}
        {status.error && (
          <p class="mt-2 text-xs text-amber" data-rfid-error>
            {t(
              status.error === 'operation_locked'
                ? 'rfid.locked'
                : status.error === 'scan_deadline_expired'
                  ? 'rfid.expired'
                  : 'rfid.capture_error'
            )}
            <span class="ml-2 font-mono">{status.error}</span>
          </p>
        )}
        {preview && (
          <div class="mt-3.5 grid gap-3.5" data-rfid-preview>
            <div class="flex flex-wrap items-center gap-3.5">
              <span class="h-8 w-8 rounded-md border border-edge" style={{ backgroundColor: preview.color }} />
              <strong>
                {preview.brand} · {preview.material}
              </strong>
              <span class="font-mono text-sm">{preview.color}</span>
            </div>
            <dl class="grid grid-cols-2 gap-x-4 gap-y-1 text-sm">
              <dt>{t('rfid.product')}</dt>
              <dd>{preview.sku}</dd>
              <dt>{t('rfid.diameter')}</dt>
              <dd>{preview.diameter} mm</dd>
              <dt>{t('rfid.nozzle')}</dt>
              <dd>
                {preview.nozzle_min}–{preview.nozzle_max} °C
              </dd>
              <dt>{t('rfid.bed')}</dt>
              <dd>
                {preview.bed_min}–{preview.bed_max} °C
              </dd>
              <dt>{t('rfid.weight')}</dt>
              <dd>{t('rfid.unknown')}</dd>
            </dl>
            <p class="text-xs text-muted">{t('rfid.applies')}</p>
            <label class="grid gap-2">
              {t('rfid.slot')}
              <Select data-rfid-slot value={slot} onChange={e => setSlot(e.currentTarget.value)}>
                <option value="">{t('rfid.choose_slot')}</option>
                {[0, 1, 2, 3].map(i => (
                  <option key={i} value={String(i)}>
                    {tpl('common.slot_n', { n: i + 1 })}
                  </option>
                ))}
              </Select>
            </label>
            <Button data-rfid-apply variant="primary" disabled={!canApply || sending} onClick={apply}>
              {t('rfid.apply')}
            </Button>
          </div>
        )}
      </div>
    </div>
  )
}
