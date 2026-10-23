import { useEffect, useRef, useState } from 'preact/hooks'
import { MoveLeft, RotateCcw, Save } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'
import { errText, notify, post, request } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { type Key, t, tpl } from '@/lib/i18n'

const I = { size: 16, strokeWidth: 1 }
const STOCK_MAX = 350 // what the stock firmware accepts

// GET /api/canvas/unload: [canvas_dev] unload_fila_dist as configured by the printer and as overridden from here
// (autosave.cfg). The printer reads it at start-up, so a saved value stays pending until the next restart.
// `extended`: the printer runs the program with the firmware's 350 mm limit raised, so max is higher.
type Unload = {
  editable: boolean
  min: number
  max: number
  extended?: boolean
  configured: number | null
  override: number | null
  pending_restart: boolean
  restart: string
}

const RESTART_ERRORS: Record<string, Key> = {
  printer_busy: 'canvas.unload_restart_busy',
  launch_failed: 'canvas.unload_restart_failed',
  reboot_failed: 'canvas.unload_restart_failed',
}

// Pull-back after the cut. Changes need an idle printer and apply after a printer restart, offered right away.
export const CanvasUnload = ({ idle, compact = false }: { idle: boolean; compact?: boolean }) => {
  const [state, setState] = useState<Unload | null>(null)
  const [value, setValue] = useState('')
  const [edited, setEdited] = useState(false) // a value outside the range read from the printer is not an input error
  const [busy, setBusy] = useState(false)
  const [restarting, setRestarting] = useState(false)
  // State alone does not guard against two clicks before the next render.
  const saveInFlight = useRef(false)
  const restartInFlight = useRef(false)
  const show = (s: Unload) => {
    setState(s)
    setValue(String(s.override ?? s.configured ?? ''))
    setEdited(false)
    const failed = RESTART_ERRORS[s.restart]
    if (failed) notify(t(failed), 'error')
  }
  // Shown only once the backend has answered with the setting; anything else hides it.
  const load = () =>
    request('/api/canvas/unload')
      .then(s => (typeof s?.min === 'number' && typeof s?.max === 'number' ? show(s) : setState(null)))
      .catch(() => setState(null))
  useEffect(() => {
    load()
  }, [])

  const restart = async () => {
    if (restartInFlight.current) return
    restartInFlight.current = true
    setRestarting(true)
    try {
      if (!(await ask(t('canvas.unload_restart_confirm'), true))) {
        notify(t('canvas.unload_pending'))
        return
      }
      await post('/api/canvas/unload/restart', 'RESTART')
      notify(t('canvas.unload_restarting'))
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      restartInFlight.current = false
      setRestarting(false)
    }
  }
  const save = async (body: string) => {
    if (saveInFlight.current || restartInFlight.current) return
    saveInFlight.current = true
    setBusy(true)
    try {
      const next: Unload = await post('/api/canvas/unload', body)
      show(next)
      notify(t('canvas.unload_saved'))
      if (next.pending_restart) await restart()
    } catch (e) {
      notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
    } finally {
      saveInFlight.current = false
      setBusy(false)
    }
  }

  if (!state) return null
  const { min, max, configured, override } = state
  const number = Number(value)
  const valid = /^\d+$/.test(value) && number >= min && number <= max
  const current = override ?? configured
  const locked = !idle || busy || restarting || !state.editable
  const canSave = !locked && valid && number !== override
  return (
    <div class={compact ? 'cc2-canvas-unload' : 'mt-3 border-t border-edge pt-3'} data-canvas-unload>
      <label for="canvas-unload" class="flex items-center gap-2">
        <MoveLeft {...I} />
        {t('canvas.unload_title')}
        {current !== null && (
          <small class="ml-auto text-muted">
            {tpl(override !== null ? 'canvas.unload_set_here' : 'canvas.unload_printer_default', { value: current })}
          </small>
        )}
      </label>
      <div class="mt-2 flex gap-2">
        <Input
          id="canvas-unload"
          type="number"
          inputMode="numeric"
          min={min}
          max={max}
          step={1}
          value={value}
          disabled={locked}
          aria-invalid={edited && !valid}
          onInput={e => {
            setValue(e.currentTarget.value.trim())
            setEdited(true)
          }}
          onKeyDown={e => {
            if (e.key === 'Enter' && canSave) {
              e.preventDefault()
              void save(value)
            }
          }}
        />
        <Button disabled={!canSave} onClick={() => save(value)}>
          <Save {...I} />
          {t('canvas.unload_save')}
        </Button>
      </div>
      <p class="mt-2 text-[13px] text-muted">
        {edited && !valid
          ? tpl('canvas.unload_invalid', { min, max })
          : tpl(compact ? 'canvas.unload_hint_compact' : 'canvas.unload_hint', { min, max })}
      </p>
      {state.extended && (
        <p class="mt-2 text-[13px] text-cyan">{tpl('canvas.unload_extended', { max, stock: STOCK_MAX })}</p>
      )}
      {configured !== null && configured > max && (
        <p class="mt-2 text-[13px] text-amber">{tpl('canvas.unload_above_range', { value: configured, max })}</p>
      )}
      {!state.editable && <p class="mt-2 text-[13px] text-amber">{t('canvas.unload_not_editable')}</p>}
      {state.pending_restart && (
        <div class="mt-2 flex flex-wrap items-center gap-2">
          <p class="text-[13px] text-cyan">{t('canvas.unload_pending')}</p>
          <Button disabled={!idle || busy || restarting || state.restart === 'restarting'} onClick={restart}>
            {t('canvas.unload_restart')}
          </Button>
        </div>
      )}
      {override !== null && (
        <Button class="mt-2" disabled={locked} onClick={() => save('default')}>
          <RotateCcw {...I} />
          {t('canvas.unload_default')}
        </Button>
      )}
    </div>
  )
}
