import { FilamentMaterial } from '@/components/filament-material'
import { useEffect, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import {
  ArrowBigDown,
  ArrowBigUp,
  Check,
  Palette,
  RefreshCw,
  Repeat,
  BrushCleaning,
  Spool,
  Trash2,
} from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Pip, Tag } from '@/components/ui/badge'
import { Row } from '@/components/shared'
import { CanvasUnload } from './canvas-unload'
import { control, errText, notify, post } from '@/lib/api'
import { type Key, t, tpl } from '@/lib/i18n'
import { poll, usePoll } from '@/lib/poll'
import { canvas, canvasColour, canvasHex, refreshCanvas } from '@/lib/canvas'
import { canvasTimeline, clearCanvasTimeline, type ObservedPrinterPhase } from '@/lib/canvas-events'
import { printer } from '@/lib/state'
import { findSpool, g, openChooser, refreshSpools, spoolLabel, spools } from '@/lib/spools'

const I = { size: 16, strokeWidth: 1 }
const MaterialDialog = ({ slot, onClose }: { slot: number; onClose: () => void }) => (
  <FilamentMaterial
    slot={slot}
    filament={canvas.get().model?.trays[slot]}
    onClose={onClose}
    onSaved={(material, color) => {
      refreshCanvas()
      if (spools.get().data?.enabled) openChooser(slot, 'material', { material, color })
    }}
  />
)

export const Canvas = () => {
  const recoverySending = useRef(false)
  const [recoveryBusy, setRecoveryBusy] = useState(false)
  const [recoverySent, setRecoverySent] = useState(false)
  const { model, controls, autoRefill, slot, optimistic, checked } = canvas.use()
  const { events } = canvasTimeline.use()
  const library = spools.use().data
  const tracking = Boolean(library?.available && library.enabled)
  const [dialog, setDialog] = useState(false)
  const [refillBusy, setRefillBusy] = useState(false)
  const [resetBusy, setResetBusy] = useState(false)
  const printerState = printer.use()
  const idle = Boolean(printerState.ok && printerState.data?.connected && printerState.data.machine?.status === 1)
  const resetAll = async () => {
    if (resetBusy || !idle || !model?.connected) return
    if (!(await ask(t('canvas.reset_all_confirm'), true))) return
    setResetBusy(true)
    try {
      if (await control('canvas:reset-all')) {
        canvas.set({ optimistic: {} })
        setTimeout(refreshCanvas, 700)
        setTimeout(() => {
          void post('/api/canvas/refresh')
            .then(refreshCanvas)
            .catch(e => notify(errText(e), 'error'))
        }, 2000)
      }
    } finally {
      setResetBusy(false)
    }
  }
  // Refresh while the page is open, except behind the material dialog.
  useEffect(() => (dialog ? undefined : poll(refreshCanvas, 2000)), [dialog])
  usePoll(refreshSpools, 10000)
  const ok = Boolean(model?.connected)
  const selected = controls.find(c => c.slot === slot)
  const recoverLoad = async () => {
    if (recoverySending.current || recoverySent || !ok || !idle || selected?.recovery_load !== true) return
    const chosen = slot
    recoverySending.current = true
    setRecoveryBusy(true)
    try {
      if (await control(`canvas:recover-load:${chosen}`, tpl('canvas.recovery_load_confirm', { slot: chosen + 1 }))) {
        setRecoverySent(true)
        void refreshCanvas()
      }
    } finally {
      recoverySending.current = false
      setRecoveryBusy(false)
    }
  }
  const stateLabels = {
    empty: 'canvas.state_empty',
    present: 'canvas.state_present',
    loaded: 'canvas.state_loaded',
    transition: 'canvas.state_transition',
    unknown: 'canvas.state_unknown',
  } as const
  const phaseLabels: Record<ObservedPrinterPhase, Key> = {
    idle: 'state.idle',
    printing: 'state.printing',
    paused: 'substate.paused',
    unknown: 'state.unknown',
  }
  const reasons: Record<string, Key> = {
    unavailable: 'canvas.reason_unavailable',
    empty: 'canvas.reason_empty',
    busy: 'canvas.reason_busy',
    head_occupied: 'canvas.reason_head_occupied',
    not_loaded: 'canvas.reason_not_loaded',
    already_loaded: 'canvas.reason_already_loaded',
    transition: 'canvas.reason_transition',
    unassigned_head: 'canvas.reason_unassigned_head',
  }
  const reason = (key?: string) => t(reasons[key || ''] || 'canvas.reason_unavailable')
  const sync = async () => {
    try {
      await post('/api/canvas/refresh')
      setTimeout(refreshCanvas, 600)
    } catch (e) {
      notify(errText(e), 'error')
    }
  }
  // Readback comes from the printer: CC2 Control asks for the Canvas state again once the change is accepted.
  const toggleRefill = async () => {
    if (autoRefill === null) return
    setRefillBusy(true)
    try {
      await post('/api/canvas/auto-refill', autoRefill ? 'off' : 'on')
      notify(t('common.command_accepted'))
      setTimeout(refreshCanvas, 1500)
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      setRefillBusy(false)
    }
  }
  const slots = [0, 1, 2, 3].map(i => {
    const tray = model?.trays[i],
      tel = tray?.filament_color
    const saved = optimistic[i]
    const pend = saved && Date.now() < saved.until && canvasHex(tel, i) !== saved.colour ? saved : undefined // drop it once expired or confirmed by telemetry
    const raw = pend ? pend.colour : tel
    // With tracking on, the remaining weight is the spool's count; the printer reports none.
    const held = tracking && library ? library.slots[i] : null
    const spool = held ? findSpool(library, held.spool) : null
    return {
      i,
      state: controls.find(c => c.slot === i)?.state || 'unknown',
      present: Number(tray?.status) > 0,
      colour:
        Number(tray?.status) > 0 && raw !== undefined && raw !== null && raw !== '' ? canvasColour(raw, i) : '#334553',
      raw,
      material: pend ? pend.material : (tray && (tray.filament_name || tray.filament_type)) || '—',
      remaining: spool
        ? `${g(spool.remaining)} g`
        : tray && tray.remaining_percent !== undefined
          ? `${tray.remaining_percent}%`
          : '—',
      held,
      spool,
      percent:
        spool && Number.isFinite(spool.net) && spool.net > 0 && Number.isFinite(spool.remaining)
          ? Math.max(0, Math.min(100, (spool.remaining / spool.net) * 100))
          : typeof tray?.remaining_percent === 'number' && Number.isFinite(tray.remaining_percent)
            ? Math.max(0, Math.min(100, tray.remaining_percent))
            : null,
    }
  })
  const ctl: [typeof ArrowBigUp, Key, () => void, boolean, string][] = [
    [
      ArrowBigUp,
      'canvas.load',
      () => control(`canvas:load:${slot}`, tpl('canvas.load_filament_from_canvas_slot', { slot: slot + 1 })) as any,
      Boolean(selected?.load && idle),
      reason(idle ? selected?.reason_load : 'busy'),
    ],
    [
      ArrowBigDown,
      'canvas.unload',
      () =>
        control(
          `canvas:unload:${slot}`,
          tpl('canvas.unload_filament_from_canvas_slot', {
            slot: slot + 1,
          })
        ) as any,
      Boolean(selected?.unload && idle),
      reason(idle ? selected?.reason_unload : 'busy'),
    ],
    [Palette, 'canvas.select_material', () => setDialog(true), idle, reason('busy')],
    [RefreshCw, 'canvas.sync', sync, true, ''],
  ]
  return (
    <Page
      title="common.canvas"
      sub="canvas.material_system_overview_and"
      tags={
        <div class="cc2-canvas-overview">
          <div class="cc2-canvas-miniatures" aria-hidden="true">
            {slots.map(s => (
              <div key={s.i} class="text-center text-xs text-muted">
                <svg viewBox="0 0 100 160" class={cn('h-12 w-8', !s.present && 'opacity-40')} aria-hidden="true">
                  <ellipse cx="50" cy="150" rx="36" ry="6" fill="currentColor" opacity="0.12" />
                  <ellipse cx="64" cy="76" rx="27" ry="67" fill="#334553" stroke="#71828e" stroke-width="2" />
                  <path d="M35 20H64C88 20 88 132 64 132H35Z" fill={s.colour} />
                  {[36, 48, 60, 72, 84, 96, 108, 120].map(y => (
                    <path key={y} d={`M36 ${y}H70`} stroke="#000" stroke-opacity="0.16" stroke-width="1.5" />
                  ))}
                  <ellipse cx="36" cy="76" rx="27" ry="67" fill="#273b49" stroke="#84939e" stroke-width="2" />
                  <ellipse cx="36" cy="76" rx="21" ry="55" fill={s.colour} />
                  <ellipse cx="36" cy="76" rx="17" ry="44" fill="none" stroke="#fff" stroke-opacity="0.22" />
                  <ellipse cx="36" cy="76" rx="12" ry="30" fill="#334553" stroke="#84939e" stroke-width="2" />
                  <ellipse cx="36" cy="76" rx="5" ry="13" fill="#14232e" />
                  <path
                    d="M25 30C15 49 15 102 25 122"
                    fill="none"
                    stroke="#fff"
                    stroke-opacity="0.3"
                    stroke-width="2"
                  />
                </svg>
                <span>{s.i + 1}</span>
              </div>
            ))}
          </div>
          <div class="grid justify-items-end gap-2">
            <Tag class="cc2-canvas-connection" tone={ok ? 'ok' : 'warning'}>
              <Pip />
              {t(ok ? 'common.connected' : 'canvas.not_detected')}
            </Tag>
            <Button onClick={sync}>
              <RefreshCw {...I} />
              {t('canvas.check_again')}
            </Button>
          </div>
        </div>
      }
    >
      <div class="mb-3.5 flex flex-wrap gap-x-4 gap-y-1 text-xs text-muted" data-canvas-status>
        <span>
          {t('canvas.telemetry')}: {t(model ? 'canvas.available' : 'canvas.unavailable')}
        </span>
        <span>
          {t('canvas.native_commands')}: {t(ok ? 'canvas.available' : 'canvas.unavailable')}
        </span>
        <span>
          {t('canvas.last_check')}: {checked || '—'}
        </span>
      </div>
      {!ok && (
        <Card class="mb-3.5">
          <CardHead icon="info" title="canvas.canvas_control_is_not_exposed_by" />
          <p class="text-muted">{t('canvas.cc2_control_will_enable_material')}</p>
        </Card>
      )}
      <div class="cc2-canvas-slots">
        {slots.map(s => (
          <Card
            key={s.i}
            data-canvas-slot={s.i}
            class={cn('cc2-canvas-slot', s.i === slot && ok && 'outline outline-2 outline-cyan')}
          >
            <button
              type="button"
              class="cc2-canvas-slot-pick"
              disabled={!ok}
              aria-label={t('canvas.select_slot')}
              aria-pressed={s.i === slot && ok}
              title={tpl('common.slot_n', { n: s.i + 1 })}
              onClick={() => canvas.set({ slot: s.i })}
            />
            <CardHead
              title="common.slot_n"
              values={{ n: s.i + 1 }}
              end={
                <span class={cn('flex items-center gap-1 text-cyan', (s.i !== slot || !ok) && 'invisible')}>
                  <Check {...I} />
                  <span class="cc2-canvas-selected-label">{t('canvas.selected')}</span>
                </span>
              }
            />
            <div
              class={cn('cc2-canvas-ring', !s.present && 'opacity-40')}
              data-slot-present={s.present}
              style={{ borderColor: s.colour, boxShadow: s.present ? `0 0 18px ${s.colour}66` : 'none' }}
            />
            {tracking && (
              <p class="cc2-canvas-spool-name" data-canvas-spool-name>
                {s.spool ? spoolLabel(s.spool) : t(s.held?.question ? 'spools.question_tag' : 'spools.not_assigned')}
              </p>
            )}
            <div class="cc2-canvas-slot-data">
              <Row label="canvas.material" value={s.material} />
              <Row label="canvas.color" value={s.raw || '—'} />
            </div>
            {tracking && (
              <Button
                class="cc2-canvas-choose"
                variant={s.held?.question ? 'primary' : 'default'}
                disabled={!ok || !s.held}
                onClick={() => openChooser(s.i, s.held?.question ? 'question' : 'manual')}
              >
                <Spool {...I} />
                {t('spools.choose')}
              </Button>
            )}
            <div class="cc2-canvas-remaining">
              <Row label="canvas.remaining" value={s.remaining} />
              <div class="cc2-canvas-weight-track" aria-hidden="true">
                {s.percent !== null && <div style={{ width: `${s.percent}%` }} />}
              </div>
            </div>
            <p
              class={cn(
                'cc2-canvas-slot-state',
                s.state === 'loaded' ? 'text-green' : s.state === 'transition' ? 'text-amber' : 'text-muted'
              )}
              data-canvas-slot-state
            >
              {t(stateLabels[s.state])}
            </p>
          </Card>
        ))}
      </div>
      <Card class="mt-3.5" data-canvas-actions>
        <div class="cc2-canvas-actionbar">
          <div class="cc2-canvas-actionheading">
            <CardHead icon="settings" title="canvas.actions_for_slot" values={{ slot: slot + 1 }} />
          </div>
          <div class="flex flex-wrap gap-2">
            {ctl.map(([Glyph, label, fn, allowed, why]) => (
              <Button
                key={label}
                data-canvas-action={label}
                variant={label === 'canvas.load' ? 'primary' : 'default'}
                disabled={!ok || !allowed}
                title={!ok ? reason('unavailable') : !allowed ? why : undefined}
                onClick={fn}
              >
                <Glyph {...I} />
                {t(label)}
              </Button>
            ))}
          </div>
        </div>
        {ok && idle && controls.some(c => c.reason_load === 'unassigned_head') && (
          <div role="status" data-canvas-recovery class="mb-3 rounded-md border border-edge bg-field p-3 text-sm">
            <p class="font-semibold">{t('canvas.recovery_title')}</p>
            <p class="mt-2 text-muted">{t('canvas.recovery_hint')}</p>
            <Button
              wide
              class="mt-3"
              data-canvas-recovery-load
              disabled={selected?.recovery_load !== true || recoveryBusy || recoverySent}
              onClick={recoverLoad}
            >
              <ArrowBigUp {...I} />
              {tpl('canvas.recovery_load_slot', { slot: slot + 1 })}
            </Button>
            <p class="mt-2 text-xs text-muted" data-canvas-recovery-feedback>
              {t(recoverySent ? 'canvas.recovery_load_sent' : 'canvas.recovery_load_select')}
            </p>
          </div>
        )}

        {(!selected?.load || !selected?.unload || !idle) && (
          <p class="mt-2 text-xs text-muted" data-canvas-action-reason>
            {!idle ? reason('busy') : !selected?.load ? reason(selected?.reason_load) : reason(selected?.reason_unload)}
          </p>
        )}
      </Card>
      <div class="cc2-canvas-lower mt-3.5">
        <Card class="cc2-canvas-settings" data-canvas-settings>
          <CardHead icon="settings" title="canvas.settings_title" />
          <div class="cc2-canvas-refill">
            <Button
              wide
              class="justify-between"
              disabled={!ok || autoRefill === null || refillBusy}
              aria-pressed={autoRefill === true}
              onClick={toggleRefill}
            >
              <span class="flex items-center gap-2">
                <Repeat {...I} />
                {t('canvas.auto_refill')}
              </span>
              <small class={autoRefill ? 'text-cyan' : 'text-muted'}>
                {autoRefill === null
                  ? t('canvas.auto_refill_not_reported')
                  : t(autoRefill ? 'canvas.auto_refill_on' : 'canvas.auto_refill_off')}
              </small>
            </Button>
            <p class="mt-1 text-xs text-muted">{t('canvas.auto_refill_hint')}</p>
          </div>
          <CanvasUnload idle={idle} compact />
          <div class="cc2-canvas-reset">
            <Button disabled={!ok || !idle || resetBusy} onClick={resetAll}>
              <BrushCleaning {...I} />
              {t('canvas.reset_all')}
            </Button>
          </div>
        </Card>
        <Card data-canvas-timeline class="cc2-canvas-history">
          <CardHead icon="history" title="canvas.timeline_title" />
          <p class="text-xs text-muted">{t('canvas.timeline_hint')}</p>
          {/* biome-ignore lint/a11y/noNoninteractiveTabindex: This bounded scroll region must be reachable for keyboard scrolling. */}
          <section class="cc2-canvas-history-scroll" aria-label={t('canvas.timeline_title')} tabIndex={0}>
            {events.length ? (
              <ol class="grid gap-2" aria-live="polite">
                {events.map(event => (
                  <li key={event.id} data-canvas-event class="rounded-md border border-edge px-3 py-2 text-sm">
                    <span class="mr-3 font-mono text-xs text-muted">{new Date(event.time).toLocaleTimeString()}</span>
                    {tpl('canvas.timeline_change', {
                      slot: event.slot + 1,
                      from: t(stateLabels[event.from]),
                      to: t(stateLabels[event.to]),
                    })}
                    <small data-canvas-printer-phase class="mt-1 block text-xs text-muted">
                      {tpl('canvas.timeline_printer_context', { state: t(phaseLabels[event.phase]) })}
                    </small>
                  </li>
                ))}
              </ol>
            ) : (
              <p data-canvas-timeline-empty class="cc2-canvas-history-empty text-xs text-muted">
                {t('canvas.timeline_empty')}
              </p>
            )}
          </section>
          <Button class="self-end" disabled={!events.length} onClick={clearCanvasTimeline}>
            <Trash2 {...I} />
            {t('canvas.timeline_clear')}
          </Button>
        </Card>
      </div>
      {dialog && (
        <MaterialDialog
          slot={slot}
          onClose={() => {
            setDialog(false)
            refreshCanvas()
          }}
        />
      )}
    </Page>
  )
}
