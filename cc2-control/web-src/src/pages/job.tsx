import { Pause, Play, Square } from 'lucide-preact'
import { useState } from 'preact/hooks'
import { ObjectBedMap } from '@/components/object-bed-map'
import { Card, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import type { ComponentChildren } from 'preact'
import { Icon } from '@/components/icons'
import { CameraCard } from '@/components/shared'
import { type Key, t, tpl } from '@/lib/i18n'
import { control } from '@/lib/api'
import { stateText } from '@/lib/machine'
import { duration, num } from '@/lib/format'
import { usePoll } from '@/lib/poll'
import { printer, refreshPrinter, view } from '@/lib/state'
import { objects, refreshObjects } from '@/lib/objects'

const I = { size: 16, strokeWidth: 1 }

// The card head without its own divider or margin; the card places the dividers.
const JobHead = ({ icon, title, end }: { icon: string; title: Key; end: ComponentChildren }) => (
  <div class="flex min-h-7 items-center gap-2.5">
    <span class="cc2-card-chip">
      <Icon n={icon} class="size-4" />
    </span>
    <h2 class="min-w-0 text-base font-semibold">{t(title)}</h2>
    <span class="ml-auto text-xs text-muted">{end}</span>
  </div>
)

export const JobControls = ({ v }: { v: ReturnType<typeof view> }) => (
  <div class="mt-4 grid grid-cols-3 gap-2.5">
    <Button class="min-w-0 px-2" disabled={!v.printing} onClick={() => control('print:pause')}>
      <Pause {...I} />
      {t('job.pause')}
    </Button>
    <Button class="min-w-0 px-2" disabled={!v.paused} onClick={() => control('print:resume')}>
      <Play {...I} />
      {t('job.resume')}
    </Button>
    <Button
      class="min-w-0 px-2"
      disabled={!(v.printing || v.paused)}
      onClick={() => control('print:cancel', t('job.cancel_the_active_print'))}
    >
      <Square {...I} />
      {t('common.cancel')}
    </Button>
  </div>
)

// The Camera button's state is kept, so the picture is still open (or closed) on the next visit.
const CAMERA_OPEN_KEY = 'cc2-job-camera'
const readCameraOpen = () => {
  try {
    return localStorage.getItem(CAMERA_OPEN_KEY) === '1'
  } catch {
    return false
  }
}
const rememberCameraOpen = (open: boolean) => {
  try {
    localStorage.setItem(CAMERA_OPEN_KEY, open ? '1' : '0')
  } catch {}
}

// A slender bar across the page: file, state, progress, layer and times, three temperatures, and the print buttons.
const JobBar = ({
  v,
  d,
  cameraOpen,
  onCamera,
  camera,
}: {
  v: ReturnType<typeof view>
  d: any
  cameraOpen: boolean
  onCamera: () => void
  camera: ComponentChildren
}) => {
  const label = (text: string) => <span class="hidden cc2-sm:inline">{text}</span>
  const stats: [string, string][] = [
    [v.active ? `${v.layer || '—'} / ${v.total || '—'}` : '—', t('common.current_layer')],
    [v.elapsedText, t('common.elapsed')],
    [v.remainingText, tpl('common.remaining_ends_time', { time: v.finishText })],
    [v.active ? duration(v.projected) : '—', t('job.est_total_print_time')],
  ]
  const temps: [string, any][] = [
    ['common.nozzle', d?.extruder?.temperature],
    ['common.heated_bed', d?.heater_bed?.temperature],
    ['dashboard.chamber', d?.chamber?.temperature],
  ]
  return (
    <div class="cc2-job-bar grid items-start gap-x-5 gap-y-3 cc2-lg:grid-cols-[minmax(14rem,1.2fr)_minmax(0,2fr)_minmax(0,32rem)]">
      <div class="min-w-0">
        <div class="flex min-w-0 items-start gap-2.5">
          <span
            title={v.rawFilename || undefined}
            class="min-w-0 line-clamp-2 text-base font-semibold [overflow-wrap:anywhere]"
          >
            {v.rawFilename || t('common.no_active_file')}
          </span>
        </div>
        {!v.active && (
          <a href="#files" class="text-[13px] text-cyan underline underline-offset-2">
            {t('common.choose_a_file_to_print')}
          </a>
        )}
      </div>
      <div class="grid min-w-0 gap-2">
        <div class="flex items-center gap-3">
          <div class="h-2 flex-1 overflow-hidden rounded-full bg-edge/70">
            <i
              class="cc2-progress-fill block h-full rounded-full bg-cyan transition-[width] duration-700"
              style={{ width: `${v.progress}%` }}
            />
          </div>
          <strong class="min-w-[3.2ch] text-right text-sm font-semibold">{Math.round(v.progress)}%</strong>
        </div>
        <div class="flex flex-wrap items-baseline gap-x-5 gap-y-1">
          {stats.map(([val, name]) => (
            <span key={name} class="text-xs">
              <strong class="text-[13px]">{val}</strong> <small class="text-muted">{name}</small>
            </span>
          ))}
          <fieldset
            class="cc2-job-temperatures m-0 flex flex-wrap gap-x-4 border-0 p-0"
            aria-label={t('common.temperatures')}
          >
            {temps.map(([name, value]) => (
              <span key={name} class="text-xs">
                <strong class="text-[13px] text-cyan">{num(value)} °C</strong>{' '}
                <small class="text-muted">{t(name as any)}</small>
              </span>
            ))}
          </fieldset>
        </div>
      </div>
      <div class="grid content-start gap-3.5">
        <div class="grid grid-cols-4 gap-2">
          <Button
            class="min-w-0 px-2"
            aria-label={t('job.pause')}
            disabled={!v.printing}
            onClick={() => control('print:pause')}
          >
            <Pause {...I} />
            {label(t('job.pause'))}
          </Button>
          <Button
            class="min-w-0 px-2"
            aria-label={t('job.resume')}
            disabled={!v.paused}
            onClick={() => control('print:resume')}
          >
            <Play {...I} />
            {label(t('job.resume'))}
          </Button>
          <Button
            class="min-w-0 px-2"
            aria-label={t('common.cancel')}
            disabled={!(v.printing || v.paused)}
            onClick={() => control('print:cancel', t('job.cancel_the_active_print'))}
          >
            <Square {...I} />
            {label(t('common.cancel'))}
          </Button>
          <Button class="min-w-0 px-2" aria-label={t('job.camera_button')} aria-pressed={cameraOpen} onClick={onCamera}>
            <Icon n="camera" class="size-4" />
            {label(t('job.camera_button'))}
          </Button>
        </div>
        {camera}
      </div>
    </div>
  )
}

export const Job = () => {
  const d = printer.use().data
  const o = objects.use()
  const v = view(d)
  const [cameraOpen, setCameraOpen] = useState(readCameraOpen)
  const toggleCamera = () => {
    rememberCameraOpen(!cameraOpen)
    setCameraOpen(!cameraOpen)
  }
  usePoll(refreshPrinter, 1500)
  usePoll(refreshObjects, 2000)

  const live = v.active && o.has && o.list.length > 0
  const endLabel = o.error
    ? t('job.objects_unavailable')
    : v.active && o.has
      ? tpl('job.n_objects_detected', { n: o.list.length })
      : t('job.no_objects_detected')
  const exclude = async () => {
    if (!v.active || !o.selected || !o.list.includes(o.selected) || o.excluded.includes(o.selected)) return
    const name = o.selected
    const label = o.entries.find(e => e.id === name)?.label || name
    if (await control(`object:exclude:${name}`, tpl('job.exclude_name_from_this_print_this', { name: label }))) {
      objects.set({ selected: '' })
      setTimeout(refreshObjects, 800)
    }
  }
  return (
    <Page title="common.job" sub="job.live_progress_camera_and_objects">
      <div class="grid gap-3.5">
        <Card class="min-w-0">
          <JobHead icon="file" title="common.current_job" end={<span class="text-cyan">{stateText(v)}</span>} />
          <div class="mt-3.5">
            <JobBar
              v={v}
              d={d}
              cameraOpen={cameraOpen}
              onCamera={toggleCamera}
              camera={
                cameraOpen ? (
                  <div class="cc2-job-camera aspect-video min-w-0">
                    <CameraCard bare />
                  </div>
                ) : null
              }
            />
          </div>
          <div class="mt-3.5 border-t border-edge pt-3.5">
            <JobHead icon="cube" title="job.object_exclusion" end={<span>{endLabel}</span>} />
            <div class="mt-3.5">
              <ObjectBedMap embedded idle={!v.active} live={live} onExclude={() => void exclude()} />
            </div>
          </div>
        </Card>
      </div>
    </Page>
  )
}
