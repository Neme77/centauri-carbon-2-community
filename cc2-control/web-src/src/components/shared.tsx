import type { ComponentChildren, VNode } from 'preact'
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks'
import { createPortal } from 'preact/compat'
import { cn } from '@/lib/utils'
import { Dot } from '@/components/ui/badge'
import { Card, CardHead } from '@/components/ui/card'
import { Icon } from '@/components/icons'
import { cameraPreferences, setPersistentCamera, type Key, t } from '@/lib/i18n'
import { errText, notify, post, tpl } from '@/lib/api'
import { poll, usePoll } from '@/lib/poll'
import { store } from '@/lib/store'
import { closeMiniplayer, miniplayerOpen, openMiniplayer } from '@/lib/miniplayer'
import { camera, printer, refreshPrinter } from '@/lib/state'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/field'

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
    <span class="text-muted">{t(label)}</span>
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
  disabled = false,
}: {
  label: Key
  pct: number
  onCommit: (v: number) => Promise<boolean>
  disabled?: boolean
}) => {
  const [v, setV] = useState(pct)
  const [draft, setDraft] = useState(String(pct))
  const [busy, setBusy] = useState(false)
  const editing = useRef(false)
  const dragging = useRef(false)
  const range = useRef<HTMLInputElement>(null)
  const pendingRange = useRef(false)
  const sending = useRef(false)
  const latest = useRef(pct)
  latest.current = pct
  useEffect(() => {
    if (editing.current || dragging.current || sending.current) return
    setV(pct)
    setDraft(String(pct))
  }, [pct])
  const restore = () => {
    setV(latest.current)
    setDraft(String(latest.current))
  }
  const commit = async (next: number) => {
    if (disabled || sending.current) return
    sending.current = true
    setBusy(true)
    try {
      if (await onCommit(next)) {
        setV(next)
        setDraft(String(next))
      } else restore()
    } finally {
      sending.current = false
      setBusy(false)
    }
  }
  useEffect(() => {
    const element = range.current
    if (!element) return
    // Preact compat maps JSX onChange to input for ranges. Listen to the
    // native change event so a mouse drag commits only when released.
    const changed = () => {
      if (!pendingRange.current) return
      pendingRange.current = false
      dragging.current = false
      void commit(Math.round(+element.value))
    }
    element.addEventListener('change', changed)
    return () => element.removeEventListener('change', changed)
  }, [commit])
  return (
    <div class="cc2-fan-row text-[13px]">
      <span>{t(label)}</span>
      <input
        ref={range}
        type="range"
        min="0"
        max="100"
        value={v}
        disabled={disabled || busy}
        aria-label={t(label)}
        onInput={e => {
          dragging.current = true
          pendingRange.current = true
          const next = Math.round(+e.currentTarget.value)
          setV(next)
          setDraft(String(next))
        }}
        onPointerDown={() => {
          dragging.current = true
        }}
        onPointerUp={() => {
          if (!pendingRange.current) dragging.current = false
        }}
        onPointerCancel={() => {
          dragging.current = false
          pendingRange.current = false
          restore()
        }}
        onBlur={() => {
          if (!dragging.current) return
          dragging.current = false
          pendingRange.current = false
          restore()
        }}
      />
      <form
        class="cc2-fan-value flex items-center gap-1.5"
        noValidate
        onSubmit={e => {
          e.preventDefault()
          const next = Number(draft)
          if (!draft.trim() || !Number.isInteger(next) || next < 0 || next > 100)
            return notify(t('control.invalid_fan_percent'), 'error')
          void commit(next)
        }}
      >
        <Input
          class="w-14 text-center tabular-nums"
          type="number"
          inputMode="numeric"
          min="0"
          max="100"
          step="1"
          value={draft}
          disabled={disabled || busy}
          aria-label={tpl('control.fan_percent', { name: t(label) })}
          onFocus={() => {
            editing.current = true
          }}
          onBlur={() => {
            editing.current = false
            restore()
          }}
          onInput={e => {
            setDraft(e.currentTarget.value)
            const next = Number(e.currentTarget.value)
            if (e.currentTarget.value.trim() && Number.isInteger(next) && next >= 0 && next <= 100) setV(next)
          }}
          onKeyDown={e => {
            if (e.key === 'Escape') {
              e.preventDefault()
              restore()
            }
          }}
        />
        <span class="text-muted">%</span>
      </form>
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
  <div class={cn('cc2-row flex justify-between gap-3 border-b border-edge py-2 text-xs', c)}>
    <span>{text ?? (label && t(label))}</span>
    <b class="min-w-0 text-right [overflow-wrap:anywhere]">{value}</b>
  </div>
)

export const Progress = ({ pct }: { pct: number }) => (
  <div class="flex items-center gap-3">
    <div class="h-3 flex-1 overflow-hidden rounded-full bg-edge/70">
      <i
        class="cc2-progress-fill block h-full rounded-full bg-cyan transition-[width] duration-700"
        style={{ width: `${pct}%` }}
      />
    </div>
    <strong class="min-w-[3.2ch] text-right text-xl font-semibold">{Math.round(pct)}%</strong>
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
const BLANK_IMAGE = 'data:image/gif;base64,R0lGODlhAQABAAAAACw='
const viewerId = () =>
  Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, '0')).join('')

// Hand-over between viewers of the same browser, so the old picture has ended before the new one starts (the
// printer only learns of a new viewer by its next poll, which left two streams open for up to a poll interval).
// 1) Same tab: every stream registers itself here; a starting stream stops the others first, synchronously.
// 2) Other tab of the same browser: a stream writes a fresh timestamp to localStorage while it runs; a starting
//    stream that sees a fresh foreign entry asks over a BroadcastChannel and waits (at most one second) until the
//    old tab says it has let go. Without either (blocked storage, no channel) it simply starts, as before.
// Another device shares nothing but the printer's claim: there the old viewer still stops on its next poll.
type Holder = { stop: (why: Key) => void; held: () => boolean }
const holders = new Set<Holder>()
const LIVE_KEY = 'cc2-camera-live'
// A hidden tab's timers can be slowed to once a minute, so its heartbeat may be that old and the tab still holds the camera.
// A stale entry nobody answers for only costs the next start at most RELEASE_WAIT_MS.
const LIVE_FRESH_MS = 75000
const LIVE_BEAT_MS = 1000
const RELEASE_WAIT_MS = 1000
let cameraChannel: BroadcastChannel | null | undefined
const channel = () => {
  if (cameraChannel === undefined) {
    try {
      cameraChannel = typeof BroadcastChannel === 'undefined' ? null : new BroadcastChannel('cc2-camera')
      cameraChannel?.addEventListener('message', event => {
        const message = event.data
        if (message?.type !== 'release' || typeof message.id !== 'string') return
        // Only a viewer that has the camera lets go, and only that tab answers (an idle tab must not end the wait before the
        // holder has let go); a viewer that is itself still starting is not touched.
        let released = false
        for (const holder of holders)
          if (holder.held()) {
            holder.stop('common.camera_taken_over')
            released = true
          }
        if (released) cameraChannel?.postMessage({ type: 'released', for: message.id })
      })
    } catch {
      cameraChannel = null
    }
  }
  return cameraChannel
}
const readLive = (): { id: string; at: number } | null => {
  try {
    const entry = JSON.parse(localStorage.getItem(LIVE_KEY) || 'null')
    return typeof entry?.id === 'string' && typeof entry?.at === 'number' ? entry : null
  } catch {
    return null
  }
}
const writeLive = (id: string | null) => {
  try {
    if (id) localStorage.setItem(LIVE_KEY, JSON.stringify({ id, at: Date.now() }))
    else localStorage.removeItem(LIVE_KEY)
  } catch {}
}
// Waits until another tab of this browser has let go of the camera (at most RELEASE_WAIT_MS); at once when none runs.
const waitForRelease = (self: string) => {
  const entry = readLive()
  const port = channel()
  if (!port || !entry || entry.id === self || Date.now() - entry.at > LIVE_FRESH_MS) return Promise.resolve()
  return new Promise<void>(resolve => {
    const done = () => {
      clearTimeout(timer)
      port.removeEventListener('message', listen)
      resolve()
    }
    const listen = (event: MessageEvent) => {
      if (event.data?.type === 'released' && event.data.for === self) done()
    }
    const timer = setTimeout(done, RELEASE_WAIT_MS)
    port.addEventListener('message', listen)
    port.postMessage({ type: 'release', id: self })
  })
}

const cameraDock = store({ slot: null as HTMLElement | null, tall: false, preferences: false })
let persistentHost: HTMLElement | null = null
let cameraParking: HTMLElement | null = null

const CameraDock = ({ tall = false, preferences = false }: { tall?: boolean; preferences?: boolean }) => {
  const slot = useRef<HTMLDivElement>(null)
  useLayoutEffect(() => {
    const element = slot.current
    if (!element) return
    cameraDock.set({ slot: element, tall, preferences })
    if (persistentHost) element.appendChild(persistentHost)
    return () => {
      // Park the existing DOM before this page is removed. Its image and
      // request stay alive; changing a portal target would recreate them.
      if (cameraDock.get().slot !== element) return
      if (persistentHost && cameraParking) cameraParking.appendChild(persistentHost)
      cameraDock.set({ slot: null })
    }
  }, [tall, preferences])
  return <div ref={slot} class="cc2-camera-dock" />
}

// Keep the same stream host when toggling persistence in the camera card.
// `bare` is the picture alone (no card, head or buttons), started when it appears and ended when it goes away.
export const CameraCard = ({
  tall,
  standalone,
  preferences,
  bare,
}: {
  tall?: boolean
  standalone?: boolean
  preferences?: boolean
  bare?: boolean
}) =>
  bare ? (
    <CameraStream standalone bare />
  ) : standalone ? (
    <CameraStream tall={tall} standalone />
  ) : (
    <CameraDock tall={tall} preferences={preferences} />
  )

const CameraPreference = () => {
  const persistent = cameraPreferences.use().persistent
  const [saving, setSaving] = useState(false)
  const [desired, setDesired] = useState(persistent)
  useEffect(() => setDesired(persistent), [persistent])
  const change = async (enabled: boolean) => {
    setDesired(enabled)
    setSaving(true)
    try {
      await setPersistentCamera(enabled)
    } catch (e) {
      setDesired(persistent)
      notify(errText(e), 'error')
    } finally {
      setSaving(false)
    }
  }
  return (
    <label class="cc2-camera-preference flex min-h-5 items-center gap-1.5 whitespace-nowrap text-xs text-muted">
      <input
        type="checkbox"
        class="size-4 shrink-0 accent-cyan"
        checked={desired}
        disabled={saving}
        onChange={e => void change(e.currentTarget.checked)}
      />
      {t('settings.camera_persistent')}
    </label>
  )
}

// What a surrounding layout (the mini player) needs to draw its own controls around the bare picture.
export type StreamControl = { on: boolean; ready: boolean; failed: boolean; gaveUp: boolean; toggle: () => void }

// `onTakenOver`: the bare picture of the mini player closes itself when another viewer takes the camera, instead
// of showing "Watch here"; `layout` places the picture and its controls.
export const CameraStream = ({
  tall,
  standalone,
  preferences,
  dock,
  bare,
  onTakenOver,
  layout,
}: {
  tall?: boolean
  standalone?: boolean
  preferences?: boolean
  dock?: HTMLElement | null
  bare?: boolean
  onTakenOver?: () => void
  layout?: (frame: VNode, control: StreamControl) => ComponentChildren
}) => {
  const persistent = cameraPreferences.use().persistent
  const keepAlive = useRef(persistent)
  keepAlive.current = persistent
  const [on, setOn] = useState(false)
  const [ready, setReady] = useState(false)
  const [failed, setFailed] = useState(false)
  const [gaveUp, setGaveUp] = useState(false)
  const [note, setNote] = useState<Key | ''>('')
  const [src, setSrc] = useState('')
  const img = useRef<HTMLImageElement>(null)
  const live = useRef(false)
  const activeSrc = useRef('')
  const generation = useRef(0)
  const retry = useRef<number | undefined>(undefined)
  const tries = useRef(0)
  const viewer = useRef('')
  if (!viewer.current) viewer.current = viewerId()
  const holder = useRef<Holder>({ stop: () => {}, held: () => false })
  const beat = useRef<number | undefined>(undefined)
  const claimed = useRef(0) // when the backend confirmed this page's claim; 0 while it has none
  const starts = useRef(0)
  const pausedHidden = useRef(false)
  const { data, at } = printer.use()
  const owner = data?.camera_viewer
  // The hint that the mini player has the picture is only true while it is open.
  const miniOpen = miniplayerOpen.use().open
  const shownNote = note === 'common.camera_in_miniplayer' && !miniOpen ? '' : note
  const clearRetry = () => {
    if (retry.current !== undefined) clearTimeout(retry.current)
    retry.current = undefined
  }
  // Letting go of a stream must end the request, not only hide it: removing the attribute is enough in Chromium, but a
  // phone's browser and the WebKit view of ElegooSlicer can keep the old multipart connection open (the camera service
  // allows only four), so the picture is first replaced by an empty one, which makes the browser drop the request.
  const release = () => {
    activeSrc.current = ''
    const element = img.current
    if (!element) return
    element.src = BLANK_IMAGE
    element.removeAttribute('src')
  }
  const stop = (why: Key | '' = '') => {
    clearInterval(beat.current)
    beat.current = undefined
    if (readLive()?.id === viewer.current) writeLive(null)
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
  // Another viewer of this browser starts: give the camera up (the mini player closes without a hint).
  holder.current.stop = why => {
    if (!live.current) return
    stop(why)
    onTakenOver?.()
  }
  holder.current.held = () => live.current && claimed.current > 0
  useEffect(() => {
    const entry = holder.current
    holders.add(entry)
    channel()
    return () => {
      holders.delete(entry)
    }
  }, [])
  const previousDock = useRef(dock)
  useEffect(() => {
    if (!standalone && !persistent && (!dock || previousDock.current !== dock) && live.current) stop()
    previousDock.current = dock
  }, [dock, persistent])
  const connect = () => {
    clearRetry()
    if (!live.current || (document.hidden && !keepAlive.current)) return
    release()
    setReady(false)
    setFailed(false)
    const next = camera(`?t=${Date.now()}-${++generation.current}`)
    activeSrc.current = next
    setSrc(next)
  }
  const start = async () => {
    if (document.hidden && !keepAlive.current) {
      // The bare picture opened on a hidden page starts as soon as the page is shown.
      if (bare) pausedHidden.current = true
      return
    }
    const attempt = ++starts.current
    clearRetry()
    live.current = true
    tries.current = 0
    claimed.current = 0
    setNote('')
    setGaveUp(false)
    setOn(true)
    for (const other of holders) if (other !== holder.current) other.stop('common.camera_taken_over')
    await waitForRelease(viewer.current)
    if (attempt !== starts.current || !live.current) return // paused or stopped while waiting
    try {
      const claim = await post('/api/camera/claim', viewer.current)
      if (claim?.viewer !== viewer.current) throw Error(t('common.camera_gave_up'))
      if (attempt === starts.current) {
        claimed.current = performance.now()
        writeLive(viewer.current)
        clearInterval(beat.current)
        beat.current = window.setInterval(() => writeLive(viewer.current), LIVE_BEAT_MS)
      }
    } catch (e) {
      if (attempt === starts.current && live.current) {
        stop('common.camera_gave_up')
        notify(tpl('common.rejected_error', { error: errText(e) }), 'error')
      }
      return
    }
    // Not when paused, hidden or unmounted while claiming, or when a newer start took over.
    if (live.current && attempt === starts.current) connect()
  }
  // The mini player was paused in a hidden tab, where it could not see another viewer take the camera. Looking at the
  // newest state first, it closes quietly if somebody else has the camera now, instead of taking it back from them.
  const resumeUnlessTaken = async () => {
    await refreshPrinter()
    const now = printer.get().data?.camera_viewer
    if (now && now !== viewer.current) onTakenOver?.()
    else await start()
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
    if (live.current && claimed.current && at >= claimed.current && owner && owner !== viewer.current) {
      stop('common.camera_taken_over')
      onTakenOver?.()
    }
  }, [owner, at])
  // Keep ownership checks alive while the persistent stream is backgrounded, and while the mini player streams
  // on a page that does not poll the printer fast enough to notice a takeover.
  // Join the shared poller only while streaming; no extra camera session.
  const watchesOwner = persistent || Boolean(onTakenOver)
  useEffect(() => {
    if (watchesOwner && on) return poll(refreshPrinter, 1500, { hidden: true })
  }, [watchesOwner, on])
  useEffect(() => {
    const hidden = () => {
      if (document.hidden && live.current && !keepAlive.current) {
        pausedHidden.current = true
        stop('common.camera_paused_hidden')
      } else if (!document.hidden && bare && pausedHidden.current) {
        // The bare picture was opened by its own button: it comes back by itself when the page is shown again.
        pausedHidden.current = false
        void (onTakenOver ? resumeUnlessTaken() : start())
      }
    }
    const leaving = () => {
      if (live.current) stop('common.camera_paused_hidden')
    }
    document.addEventListener('visibilitychange', hidden)
    window.addEventListener('pagehide', leaving)
    if (standalone) void start() // opening the camera window (or the bare picture) is the explicit start
    return () => {
      document.removeEventListener('visibilitychange', hidden)
      window.removeEventListener('pagehide', leaving)
      clearInterval(beat.current)
      if (readLive()?.id === viewer.current) writeLive(null)
      live.current = false
      clearRetry()
      release()
    }
  }, [])
  const frame = (
    <div
      class={cn(
        'cc2-camera-frame relative grid place-items-center overflow-hidden rounded-md border border-edge bg-black',
        bare
          ? 'cc2-camera-bare size-full'
          : standalone
            ? 'min-h-[calc(100dvh_-_7rem)]'
            : tall
              ? 'min-h-80'
              : 'aspect-video',
        !ready && 'cc2-camera-paused'
      )}
    >
      {!ready && <div class="cc2-camera-grid" aria-hidden="true" />}
      {!ready && (
        <div class="cc2-camera-idle-controls absolute inset-x-0 z-10 max-w-full px-4 py-3 text-center text-muted">
          {(on || shownNote) && (
            <p class="text-xs">
              {on
                ? t(
                    gaveUp
                      ? 'common.camera_gave_up'
                      : failed
                        ? 'common.camera_unavailable'
                        : 'common.waiting_for_camera_stream'
                  )
                : shownNote && t(shownNote)}
            </p>
          )}
          {!on && (!bare || (shownNote && shownNote !== 'common.camera_paused_hidden')) && (
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
  )
  if (bare)
    return layout ? layout(frame, { on, ready, failed, gaveUp, toggle: () => (on ? stop() : void start()) }) : frame
  return (
    <Card class="cc2-camera-card flex flex-col">
      <CardHead
        icon="camera"
        title="common.live_camera"
        end={
          <span class="flex flex-wrap items-center justify-end gap-2">
            {preferences && !standalone ? <CameraPreference /> : null}
            {on && (
              <>
                {ready && (
                  <span class="hidden items-center gap-1 cc2-sm:inline-flex">
                    <Dot /> {t('common.live')}
                  </span>
                )}
                <Button
                  class="cc2-camera-stop min-w-8 px-2 text-xs"
                  aria-label={t('common.camera_stop')}
                  title={t('common.camera_stop')}
                  onClick={() => stop()}
                >
                  <Icon n="pause" class="size-4" />
                  <span class="hidden cc2-sm:inline">{t('common.camera_stop')}</span>
                </Button>
              </>
            )}
          </span>
        }
      />
      {frame}
      {!standalone && (
        <div class="mt-3 grid gap-2.5 cc2-sm:grid-cols-3">
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
            variant={miniOpen ? 'active' : 'default'}
            data-miniplayer-open
            aria-pressed={miniOpen}
            onClick={() => {
              // A second press closes it again (its stream ends; this card stays as it is, it does not start by itself).
              if (miniOpen) return closeMiniplayer()
              // The mini player takes the camera over from this card, which then simply waits.
              if (live.current) stop('common.camera_in_miniplayer')
              openMiniplayer()
            }}
          >
            <Icon n="pip" class="size-4" />
            {t('common.miniplayer_open')}
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

export const PersistentCamera = () => <PersistentCameraHost />

const PersistentCameraHost = () => {
  const [host] = useState(() => document.createElement('div'))
  const parking = useRef<HTMLDivElement>(null)
  const { slot, tall, preferences } = cameraDock.use()
  useLayoutEffect(() => {
    persistentHost = host
    cameraParking = parking.current
    host.className = 'cc2-persistent-camera'
    ;(cameraDock.get().slot || cameraParking)?.appendChild(host)
    return () => {
      persistentHost = null
      cameraParking = null
      host.remove()
    }
  }, [host])
  return (
    <>
      <div ref={parking} hidden class="cc2-camera-parking" />
      {createPortal(<CameraStream tall={tall} preferences={preferences} dock={slot} />, host)}
    </>
  )
}
