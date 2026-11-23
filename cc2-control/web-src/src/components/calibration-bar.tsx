import type { ComponentChildren } from 'preact'
import { useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Icon } from '@/components/icons'
import { errText, notify, post } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { usePoll } from '@/lib/poll'
import { plates, refreshPlates, stopCalibration } from '@/lib/plates'
import { screwSoakOf } from '@/lib/screw-soak'
import { consoleStatus, printer, refreshConsole, refreshPrinter } from '@/lib/state'
import { liveNumber } from '@/lib/temp-values'

const clock = (seconds: number) => `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`

// While CC2 Control runs a bed mesh calibration on the printer, this line stands in the field at the top of every page
// (components/guided-chain: MeshStatus, which also reads the library): what it does now, and "Stop" until the probing
// starts. Alone in the field it has the normal size; under the line of the guided chain it is `compact`.
export const CalibrationLine = ({ compact = false, end }: { compact?: boolean; end?: ComponentChildren }) => {
  const { data: library } = plates.use()
  const job = library?.calibration
  const running = Boolean(job && job.stage !== 'off')
  const [stopping, setStopping] = useState(false)
  // The program reports the seconds when it answered (plates.at); in between the line counts on its own, every second.
  const [now, setNow] = useState(() => Date.now())
  usePoll(() => setNow(Date.now()), 1000)
  const since = Math.max(0, Math.floor((now - plates.get().at) / 1000))
  if (!job || !library || !running) return null
  const detail =
    job.stage === 'homing'
      ? t('bed.cal_homing')
      : job.stage === 'heating'
        ? tpl('bed.cal_heating', { bed: job.bed === null ? '—' : Math.round(job.bed), temp: job.temp })
        : job.stage === 'soaking'
          ? tpl('bed.cal_soaking', { temp: job.temp, left: clock(Math.max(0, job.remaining - since)) })
          : job.stage === 'probing'
            ? tpl('bed.cal_probing', { side: job.side, temp: job.temp })
            : tpl('bed.cal_saving', { name: library.plates.find(p => p.id === job.plate)?.name || '' })
  return (
    <div
      data-calibration-bar={job.stage}
      data-calibration-compact={compact ? '' : undefined}
      class={`flex flex-wrap items-center gap-x-3 gap-y-1.5 text-cyan ${compact ? 'text-[11px]' : 'text-xs'}`}
    >
      <Icon n="bolt" class={`shrink-0 ${compact ? 'size-3.5' : 'size-4.5'}`} />
      <span class="min-w-0 flex-1 basis-60 break-words">
        <strong class="font-semibold">{t('bed.cal_bar_running')}</strong> · <span>{detail}</span>
        {job.stage === 'probing' && (
          <>
            {' '}
            <span class="tabular-nums" data-calibration-elapsed>
              {clock(job.elapsed + since)}
            </span>
          </>
        )}
      </span>
      {/* Also the mesh of "Measure a new mesh" (one mesh, then the screw points or the session) is stopped here: the chain then ends by itself. */}
      {['homing', 'heating', 'soaking'].includes(job.stage) && (
        <Button
          class={compact ? 'min-h-8 px-2.5 text-[11px]' : 'min-h-9 px-3 text-xs'}
          data-calibration-stop
          disabled={stopping}
          onClick={async () => {
            setStopping(true)
            try {
              await stopCalibration()
            } catch (e) {
              notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
            } finally {
              setStopping(false)
              void refreshPlates()
            }
          }}
        >
          {t('bed.cal_stop')}
        </Button>
      )}
      {end}
    </div>
  )
}

// Cancels the running screw measurement (quick or in a session). The printer service ends it after the line that is
// running, lifts the nozzle and switches the heaters off; a line that waits (heating, nozzle cleaning) ends first.
export const ScrewCancelButton = () => {
  const status = consoleStatus.use()
  const [sending, setSending] = useState(false)
  if (!status.busy || !status.cancellable) return null
  const cancelling = status.cancelling || sending
  return (
    <span class="flex flex-wrap items-center gap-x-2 gap-y-1" data-screw-cancel-box>
      <Button
        class="min-h-9 px-3 text-xs"
        data-screw-cancel
        disabled={cancelling}
        onClick={async () => {
          setSending(true)
          try {
            await post('/api/control', 'screws:cancel')
          } catch (e) {
            notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
          } finally {
            await refreshConsole()
            setSending(false)
          }
        }}
      >
        {t('bed.measure_cancel')}
      </Button>
      {cancelling && (
        <span class="text-xs text-muted" role="status" data-screw-cancelling>
          {t('bed.measure_cancelling')}
        </span>
      )}
    </span>
  )
}

// A screw measurement outside a session (quick, one corner, or the screw points of the guided chain): this line stands
// in the field at the top of every page (components/guided-chain: MeshStatus, which also reads the console), in the
// accent colour like the calibration; a measurement of a session stays in the orange bar of the session. Cancel is here.
// Directly after a mesh the printer service measures without a second nozzle cleaning (screws_after_mesh): the line
// says so with a short addition.
export const ScrewMeasureLine = ({ compact = false, end }: { compact?: boolean; end?: ComponentChildren }) => {
  const afterMesh = Boolean(printer.use().data?.screws_after_mesh)
  return (
    <div
      data-screw-measure-bar
      class={`flex flex-wrap items-center gap-x-3 gap-y-1.5 text-cyan ${compact ? 'text-[11px]' : 'text-xs'}`}
    >
      <Icon n="target" class={`shrink-0 ${compact ? 'size-3.5' : 'size-4.5'}`} />
      <span class="min-w-0 flex-1 basis-60 break-words">
        <strong class="font-semibold">{t('bed.measure_bar_running')}</strong>
        {afterMesh && <span data-screw-after-mesh> · {t('bed.measure_after_mesh')}</span>} ·{' '}
        <span>{t('bed.measure_bar_detail')}</span>
      </span>
      <ScrewCancelButton />
      {end}
    </div>
  )
}

// The hold time before a screw measurement or a session start (lib/screw-soak): the printer service heats the bed and
// holds it before it starts. This line stands in the field at the top of every page (components/guided-chain:
// MeshStatus), in the accent colour, also for a session start until the session is open. "Stop" ends the hold with the
// same action as the cancel of a measurement (screws:cancel); nothing is measured then.
export const ScrewSoakLine = ({ compact = false, end }: { compact?: boolean; end?: ComponentChildren }) => {
  const data = printer.use().data
  const soak = screwSoakOf(data)
  const [stopping, setStopping] = useState(false)
  // The service reports the seconds left when it answered (printer.at); in between the line counts on its own.
  const [now, setNow] = useState(() => performance.now())
  usePoll(() => setNow(performance.now()), 1000)
  if (!soak) return null
  const since = Math.max(0, Math.floor((now - printer.get().at) / 1000))
  // The service reports the target; the measured bed temperature is the printer's, as everywhere.
  const live = liveNumber(data?.heater_bed?.temperature)
  const target = soak.bed == null ? '—' : Math.round(soak.bed)
  const detail =
    soak.stage === 'heating'
      ? tpl('bed.cal_heating', { bed: live == null ? '—' : Math.round(live), temp: target })
      : tpl('bed.cal_soaking', { temp: target, left: clock(Math.max(0, soak.remaining - since)) })
  return (
    <div
      data-screw-soak-bar={soak.stage}
      data-screw-soak-action={soak.action}
      class={`flex flex-wrap items-center gap-x-3 gap-y-1.5 text-cyan ${compact ? 'text-[11px]' : 'text-xs'}`}
    >
      <Icon n="temp" class={`shrink-0 ${compact ? 'size-3.5' : 'size-4.5'}`} />
      <span class="min-w-0 flex-1 basis-60 break-words">
        <strong class="font-semibold">
          {t(soak.action === 'session' ? 'bed.soak_bar_session' : 'bed.soak_bar_measure')}
        </strong>{' '}
        · <span data-screw-soak-detail>{detail}</span>
      </span>
      <Button
        class={compact ? 'min-h-8 px-2.5 text-[11px]' : 'min-h-9 px-3 text-xs'}
        data-screw-soak-stop
        disabled={stopping}
        onClick={async () => {
          setStopping(true)
          try {
            await post('/api/control', 'screws:cancel')
          } catch (e) {
            notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
          } finally {
            await Promise.all([refreshPrinter(), refreshConsole()])
            setStopping(false)
          }
        }}
      >
        {t('bed.cal_stop')}
      </Button>
      {end}
    </div>
  )
}
