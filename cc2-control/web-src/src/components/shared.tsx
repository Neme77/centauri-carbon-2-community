import type { ComponentChildren } from 'preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { Dot } from '@/components/ui/badge'
import { Card, CardHead } from '@/components/ui/card'
import { Icon } from '@/components/icons'
import { type Key, t } from '@/lib/i18n'
import { post } from '@/lib/api'
import { usePoll } from '@/lib/poll'
import { camera, printer, refreshPrinter } from '@/lib/state'
import { Button } from '@/components/ui/button'

export const Reading = ({
  dot,
  label,
  value,
  target,
}: {
  dot?: 'red' | 'blue' | 'amber'
  label: Key
  value: string
  target?: string
}) => (
  <div class="flex items-center gap-2 text-xs">
    {dot && <Dot c={dot} />}
    {t(label)}
    <b class="ml-auto text-[13px] font-semibold">{value}</b>
    {target && <span class="min-w-6 text-right text-xs text-muted">{target}</span>}
  </div>
)

export const FanBar = ({ label, pct }: { label: Key; pct: number }) => (
  <div class="my-2.5 grid grid-cols-[1fr_2.5rem_1fr] items-center gap-2 text-xs">
    <span>{t(label)}</span>
    <b>{pct}%</b>
    <div class="h-1.5 rounded bg-edge">
      <i class="block size-3 -translate-y-[3px] rounded-full bg-cyan" style={{ marginLeft: `${pct}%` }} />
    </div>
  </div>
)

export const FanSlider = ({
  label,
  pct,
  onCommit,
}: {
  label: Key
  pct: number
  onCommit: (v: number) => Promise<boolean>
}) => {
  const [v, setV] = useState(pct)
  useEffect(() => setV(pct), [pct])
  return (
    <div class="my-4 grid grid-cols-[1fr_2.5rem_1fr] items-center gap-2 text-[13px]">
      <span>{t(label)}</span>
      <b>{v}%</b>
      <input
        type="range"
        min="0"
        max="100"
        value={v}
        aria-label={t(label)}
        onInput={e => setV(+e.currentTarget.value)}
        onChange={async e => {
          const next = Math.round(+e.currentTarget.value)
          if (!(await onCommit(next))) setV(pct)
        }}
      />
    </div>
  )
}

export const Row = ({
  label,
  text,
  value,
  class: c,
}: {
  label?: Key
  text?: string
  value: ComponentChildren
  class?: string
}) => (
  <div class={cn('flex justify-between gap-3 border-b border-edge py-2 text-xs', c)}>
    <span>{text ?? (label && t(label))}</span>
    <b class="min-w-0 text-right [overflow-wrap:anywhere]">{value}</b>
  </div>
)

export const Progress = ({ pct }: { pct: number }) => (
  <div class="flex items-center gap-3">
    <div class="h-3 flex-1 overflow-hidden rounded-full bg-edge">
      <i class="block h-full rounded-full bg-cyan" style={{ width: `${pct}%` }} />
    </div>
    <strong class="text-lg">{Math.round(pct)}%</strong>
  </div>
)

export const Notice = ({ icon = 'info', children }: { icon?: string; children: ComponentChildren }) => (
  <div class="mt-3 flex items-center gap-2.5 rounded-md border border-edge bg-field/60 px-3 py-2.5 text-xs text-muted">
    <Icon n={icon} class="size-4.5 text-cyan" />
    {children}
  </div>
)

export const Warn = ({ children }: { children: ComponentChildren }) => (
  <div class="mt-3 rounded-md border border-amber bg-amber/10 p-2.5 text-xs text-amber">{children}</div>
)

export const Muted = ({ children, class: c }: { children: ComponentChildren; class?: string }) => (
  <p class={cn('text-muted', c)}>{children}</p>
)

// Each card owns one MJPEG request; stopping the view leaves the printer camera service running.
// Live view runs on one CC2 Control page at a time: starting it claims the camera, and a page that sees
// another viewer in /api/printer stops its own stream and offers to take it back, instead of retrying.
const CAMERA_RETRY_MS = [2500, 5000, 10000, 20000, 30000]
const viewerId = () =>
  Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, '0')).join('')

export const CameraCard = ({ tall, standalone }: { tall?: boolean; standalone?: boolean }) => {
  const [on, setOn] = useState(false)
  const [ready, setReady] = useState(false)
  const [failed, setFailed] = useState(false)
  const [gaveUp, setGaveUp] = useState(false)
  const [note, setNote] = useState<Key | ''>('')
  const [src, setSrc] = useState('')
  const frame = useRef<HTMLDivElement>(null)
  const img = useRef<HTMLImageElement>(null)
  const live = useRef(false)
  const visible = useRef(true)
  const activeSrc = useRef('')
  const generation = useRef(0)
  const retry = useRef<number | undefined>(undefined)
  const tries = useRef(0)
  const viewer = useRef('')
  if (!viewer.current) viewer.current = viewerId()
  const claimed = useRef(0) // when the backend confirmed this page's claim; 0 while it has none
  const starts = useRef(0)
  const { data, at } = printer.use()
  const owner = data?.camera_viewer
  const clearRetry = () => {
    if (retry.current !== undefined) clearTimeout(retry.current)
    retry.current = undefined
  }
  const release = () => {
    activeSrc.current = ''
    img.current?.removeAttribute('src')
  }
  const stop = (why: Key | '' = '') => {
    live.current = false
    claimed.current = 0
    clearRetry()
    release()
    setSrc('')
    setOn(false)
    setReady(false)
    setFailed(false)
    setGaveUp(false)
    setNote(why)
  }
  const connect = () => {
    clearRetry()
    if (!live.current || document.hidden || !visible.current) return
    release()
    setReady(false)
    setFailed(false)
    const next = camera(`?t=${Date.now()}-${++generation.current}`)
    activeSrc.current = next
    setSrc(next)
  }
  const start = async () => {
    if (document.hidden || !visible.current) return
    const attempt = ++starts.current
    clearRetry()
    live.current = true
    tries.current = 0
    claimed.current = 0
    setNote('')
    setGaveUp(false)
    setOn(true)
    try {
      await post('/api/camera/claim', viewer.current)
      if (attempt === starts.current) claimed.current = performance.now()
    } catch {
      /* stream anyway: without a confirmed claim this page just does not watch for other viewers */
    }
    // Not when paused, hidden or unmounted while claiming, or when a newer start took over.
    if (live.current && attempt === starts.current) connect()
  }
  const current = (element: HTMLImageElement) =>
    live.current && element === img.current && element.getAttribute('src') === activeSrc.current
  const lost = (element: HTMLImageElement) => {
    if (!current(element) || retry.current !== undefined) return
    setReady(false)
    setFailed(true)
    release()
    setSrc('')
    if (tries.current >= CAMERA_RETRY_MS.length) {
      setGaveUp(true)
      return
    }
    retry.current = window.setTimeout(connect, CAMERA_RETRY_MS[tries.current++])
  }
  // Another page claimed the camera after this one: hand the stream over. Replies to requests sent before
  // this page's claim was confirmed still show the previous viewer, so they are ignored.
  useEffect(() => {
    if (live.current && claimed.current && at >= claimed.current && owner && owner !== viewer.current)
      stop('common.camera_taken_over')
  }, [owner, at])
  useEffect(() => {
    const hidden = () => {
      if (document.hidden && live.current) stop('common.camera_paused_hidden')
    }
    const leaving = () => {
      if (live.current) stop('common.camera_paused_hidden')
    }
    const observer = new IntersectionObserver(([entry]) => {
      visible.current = entry.isIntersecting
      if (!entry.isIntersecting && live.current) stop('common.camera_paused_out_of_view')
    })
    if (frame.current) observer.observe(frame.current)
    document.addEventListener('visibilitychange', hidden)
    window.addEventListener('pagehide', leaving)
    if (standalone) void start() // opening the camera window is the explicit start
    return () => {
      document.removeEventListener('visibilitychange', hidden)
      window.removeEventListener('pagehide', leaving)
      observer.disconnect()
      live.current = false
      clearRetry()
      release()
    }
  }, [])
  return (
    <Card class="flex flex-col">
      <CardHead
        icon="camera"
        title="common.live_camera"
        end={
          on ? (
            <>
              {ready && (
                <>
                  <Dot /> {t('common.live')}
                </>
              )}
              <Button class="ml-2 text-xs" onClick={() => stop()}>
                {t('common.camera_stop')}
              </Button>
            </>
          ) : undefined
        }
      />
      <div
        ref={frame}
        class={cn(
          'cc2-camera-frame relative grid place-items-center overflow-hidden rounded-md border border-edge bg-black',
          standalone ? 'min-h-[calc(100dvh_-_7rem)]' : tall ? 'min-h-80' : 'aspect-video'
        )}
      >
        {!ready && (
          <div class="text-center text-muted">
            <Icon n="camera" class="mx-auto mb-2 size-10" />
            <strong class="block text-[15px] font-medium">
              {t(tall ? 'common.your_live_print_camera' : 'common.your_cc2_camera_feed')}
            </strong>
            <p class="text-xs">
              {on
                ? t(
                    gaveUp
                      ? 'common.camera_gave_up'
                      : failed
                        ? 'common.camera_unavailable'
                        : 'common.waiting_for_camera_stream'
                  )
                : t(note || 'common.camera_off')}
            </p>
            {!on && (
              <Button class="mt-2 text-xs" onClick={start}>
                {t(note === 'common.camera_taken_over' ? 'common.camera_watch_here' : 'common.camera_start')}
              </Button>
            )}
            {on && failed && (
              <Button class="mt-2 text-xs" onClick={start}>
                {t('common.camera_retry_now')}
              </Button>
            )}
          </div>
        )}
        {on && src && (
          <img
            key={src}
            ref={img}
            src={src}
            alt={t('common.cc2_live_camera')}
            class={cn('absolute inset-0 size-full object-contain', !ready && 'invisible')}
            onLoad={event => {
              if (!current(event.currentTarget)) return
              clearRetry()
              tries.current = 0
              setReady(true)
              setFailed(false)
              setGaveUp(false)
            }}
            onError={event => lost(event.currentTarget)}
          />
        )}
      </div>
      {!standalone && (
        <div class="mt-3 grid gap-2.5 cc2-sm:grid-cols-2">
          <Button
            class="h-auto min-w-0 whitespace-normal px-2 py-2 text-center text-xs cc2-sm:text-sm"
            onClick={() => {
              stop()
              // A CC2 Control camera window rather than the raw stream, so it follows the one-viewer rule.
              window.open('#camera', 'cc2-camera')
            }}
          >
            <Icon n="open" class="size-4" />
            {t('common.open_in_new_window')}
          </Button>
          <Button
            class="h-auto min-w-0 whitespace-normal px-2 py-2 text-center text-xs cc2-sm:text-sm"
            onClick={() => window.open(camera(`?snapshot=${Date.now()}`), 'cc2-snapshot')}
          >
            <Icon n="camera" class="size-4" />
            {t('common.snapshot')}
          </Button>
        </div>
      )}
    </Card>
  )
}

// The separate camera window (#camera): the camera card alone, under the same one-viewer rule.
export const CameraWindow = () => {
  usePoll(refreshPrinter, 1500)
  useEffect(() => {
    document.title = t('common.live_camera')
  })
  return (
    <main class="mx-auto max-w-[2000px] p-3 md:p-4">
      <CameraCard tall standalone />
    </main>
  )
}
