import { GripVertical, Minus, Pause, Play, Plus, X } from 'lucide-preact'
import type { VNode } from 'preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import { Dot } from '@/components/ui/badge'
import { CameraStream, type StreamControl } from '@/components/shared'
import { i18n, t, tpl } from '@/lib/i18n'
import {
  MINI_BAR,
  MINI_MARGIN,
  MINI_ZOOM_STEPS,
  clampMiniWidth,
  clampPan,
  clampZoom,
  closeMiniplayer,
  miniDefaultWidth,
  miniHeight,
  miniWidthLimits,
  miniplayer,
} from '@/lib/miniplayer'

const KEY_STEP = 16
const KEY_STEP_BIG = 64
const MIN_ROOM = 96 // less room than this towards the top or left, and the size handle grows the window the other way
const between = (value: number, low: number, high: number) => Math.min(high, Math.max(low, value))

const viewportSize = () => ({ w: document.documentElement.clientWidth, h: document.documentElement.clientHeight })

// The window may not be taller than what is left below the header: the width is cut to fit (16:9 plus the bar), but never
// below the smallest size.
const fitHeight = (width: number, viewportWidth: number, viewportHeight: number, top: number) =>
  Math.max(
    miniWidthLimits(viewportWidth).min,
    Math.min(width, Math.floor(((viewportHeight - top - MINI_MARGIN - MINI_BAR) * 16) / 9))
  )

// The area the window may occupy: the whole browser window with a small margin. It is drawn above the header and the side
// menu (a phone's menu drawer, dialogs and notices stay above it), so it can be put anywhere, like a picture-in-picture.
const area = () => ({ left: MINI_MARGIN, top: MINI_MARGIN })

// A mouse move that reports no pressed button: the release happened where the page could not see it (outside the browser
// window, in another app, a focus change). Such a move ends a drag instead of following the pointer.
const buttonLost = (e: PointerEvent) => e.pointerType === 'mouse' && e.buttons === 0

// The emergency stop (and on a phone the menu button) in the header is a locked area: the window may be put anywhere else,
// but it slides around these buttons instead of covering them. Of the four ways out (left, right, above, below) the
// shortest one that stays in the browser window is taken.
const KEEP_OUT = ['.cc2-emergency-stop', '#cc2-menu-toggle']
const KEEP_OUT_GAP = 4
type Box = { left: number; right: number; top: number; bottom: number }
const lockedAreas = (): Box[] =>
  KEEP_OUT.flatMap(selector => {
    const r = document.querySelector(selector)?.getBoundingClientRect()
    if (!r || r.width === 0 || r.height === 0) return []
    return [
      {
        left: r.left - KEEP_OUT_GAP,
        right: r.right + KEEP_OUT_GAP,
        top: r.top - KEEP_OUT_GAP,
        bottom: r.bottom + KEEP_OUT_GAP,
      },
    ]
  })
const hits = (l: number, t: number, w: number, h: number, a: Box) =>
  l < a.right && l + w > a.left && t < a.bottom && t + h > a.top
// All locked areas are solved together: the candidates are the wanted place and every place that touches the edge of one
// of the areas (left of it, right of it, above it, below it), in every combination; the nearest one that hits none of them
// and stays in the browser window wins. `ok` is false when there is none (a very low window); then the nearest place that
// hits none of them wins even if it is partly outside, because the lock comes first.
const keepOut = (left: number, top: number, width: number, height: number) => {
  const areas = lockedAreas()
  if (areas.length === 0 || !areas.some(a => hits(left, top, width, height, a))) return { l: left, t: top, ok: true }
  const now = viewportSize()
  const b = area()
  const maxLeft = Math.max(b.left, now.w - width - MINI_MARGIN)
  const maxTop = Math.max(b.top, now.h - height - MINI_MARGIN)
  const ls = [left, ...areas.flatMap(a => [a.left - width, a.right])]
  const ts = [top, ...areas.flatMap(a => [a.top - height, a.bottom])]
  const all = ls.flatMap(l => ts.map(t => ({ l, t }))).filter(c => !areas.some(a => hits(c.l, c.t, width, height, a)))
  const near = (list: { l: number; t: number }[]) =>
    list.sort((p, q) => Math.abs(p.l - left) + Math.abs(p.t - top) - (Math.abs(q.l - left) + Math.abs(q.t - top)))[0]
  const inside = near(all.filter(c => c.l >= b.left && c.l <= maxLeft && c.t >= b.top && c.t <= maxTop))
  if (inside) return { ...inside, ok: true }
  const any = near(all)
  return any ? { ...any, ok: false } : { l: left, t: top, ok: false }
}
// The lowest bottom edge of the locked areas: a window that is not too tall always fits below it.
const lockedBottom = () => Math.max(0, ...lockedAreas().map(a => a.bottom))
// Place and width together: where no place is free, the window gets smaller (never below the smallest size) so that it
// fits below the locked areas.
const lockedPlace = (left: number, top: number, width: number) => {
  let w = width
  let spot = keepOut(left, top, w, miniHeight(w))
  if (!spot.ok) {
    const now = viewportSize()
    w = fitHeight(w, now.w, now.h, lockedBottom())
    spot = keepOut(left, top, w, miniHeight(w))
  }
  return { ...spot, w }
}

type Pointer = { x: number; y: number }
type Gesture =
  | { kind: 'pan'; x: number; y: number; tx: number; ty: number }
  | { kind: 'pinch'; distance: number; zoom: number; mx: number; my: number; tx: number; ty: number }

const zoomText = (zoom: number) =>
  `${new Intl.NumberFormat(i18n.get().lang, { minimumFractionDigits: 1, maximumFractionDigits: 1 }).format(zoom)}×`

// The live camera in a small window that stays above every page. It is one more place for the same camera view,
// not a second stream: it claims the camera like any viewer, so another viewer starting its picture closes it.
export const MiniPlayer = () => {
  const state = miniplayer.use()
  i18n.use()
  const [viewport, setViewport] = useState(viewportSize)
  useEffect(() => {
    const resized = () => setViewport(viewportSize())
    addEventListener('resize', resized)
    return () => removeEventListener('resize', resized)
  }, [])

  const bounds = area()
  const widthWanted = fitHeight(
    clampMiniWidth(state.w ?? miniDefaultWidth(viewport.w), viewport.w),
    viewport.w,
    viewport.h,
    bounds.top
  )
  const maxLeft = Math.max(bounds.left, viewport.w - widthWanted - MINI_MARGIN)
  const maxTop = Math.max(bounds.top, viewport.h - miniHeight(widthWanted) - MINI_MARGIN)
  const wantedLeft = between(state.x === null ? maxLeft - MINI_MARGIN : state.x * viewport.w, bounds.left, maxLeft)
  const wantedTop = between(state.y === null ? maxTop - MINI_MARGIN : state.y * viewport.h, bounds.top, maxTop)
  const { l: left, t: top, w: width } = lockedPlace(wantedLeft, wantedTop, widthWanted)
  const frameHeight = Math.round((width * 9) / 16)
  const zoom = clampZoom(state.zoom)
  const panX = clampPan(state.panX, width, zoom)
  const panY = clampPan(state.panY, frameHeight, zoom)
  const limits = miniWidthLimits(viewport.w)

  // The handlers read the newest numbers through a ref, as pointer events arrive between two renders.
  const geometry = useRef({ width, frameHeight, left, top, bounds })
  geometry.current = { width, frameHeight, left, top, bounds }

  const place = (nextLeft: number, nextTop: number, nextWidth = geometry.current.width) => {
    const now = viewportSize()
    const b = area()
    const l = between(nextLeft, b.left, Math.max(b.left, now.w - nextWidth - MINI_MARGIN))
    const tp = between(nextTop, b.top, Math.max(b.top, now.h - miniHeight(nextWidth) - MINI_MARGIN))
    const free = lockedPlace(l, tp, nextWidth)
    miniplayer.set({ x: free.l / now.w, y: free.t / now.h })
    return free.w
  }
  // The size handle is the top left corner, so by default the corner opposite to it (bottom right) stays where it is.
  // Where there is no room towards the top or the left (the window sits against the edge of the browser window), that side
  // gives way to the other one: the window then keeps its top or left edge and grows down or to the right, so it can be
  // enlarged everywhere and never leaves the visible area. Each direction is decided once, when a drag starts.
  // The pointer also stops at what fits (`fit`); the keys have no such limit and move the window in instead, so they
  // always reach the largest size.
  type Anchor = { right: number; bottom: number; left: number; top: number; fromRight: boolean; fromBottom: boolean }
  const resize = (nextWidth: number, anchor: Anchor, fit = false) => {
    const now = viewportSize()
    const b = area()
    // Never taller than the screen shows (a low window), also for the keys.
    let w = fitHeight(clampMiniWidth(nextWidth, now.w), now.w, now.h, b.top)
    if (fit) {
      // The side that grows towards the top or the left stops at the edge of the browser window; the other side is already
      // limited by the width limits and the height above.
      const across = anchor.fromRight ? anchor.right - b.left : Number.POSITIVE_INFINITY
      const down = anchor.fromBottom ? anchor.bottom - b.top : Number.POSITIVE_INFINITY
      const room = Math.min(across, ((down - MINI_BAR) * 16) / 9)
      w = Math.max(miniWidthLimits(now.w).min, Math.min(w, Math.floor(room)))
    }
    const placed = place(
      anchor.fromRight ? anchor.right - w : anchor.left,
      anchor.fromBottom ? anchor.bottom - miniHeight(w) : anchor.top,
      w
    )
    miniplayer.set({ w: placed })
  }
  const currentAnchor = (): Anchor => {
    const g = geometry.current
    const now = viewportSize()
    const b = area()
    const right = g.left + g.width
    const bottom = g.top + miniHeight(g.width)
    // Room left for growing, towards the top left (the default) and towards the bottom right.
    const toTopLeft = [right - b.left - g.width, ((bottom - b.top - MINI_BAR) * 16) / 9 - g.width]
    const toBottomRight = [
      now.w - MINI_MARGIN - g.left - g.width,
      ((now.h - MINI_MARGIN - g.top - MINI_BAR) * 16) / 9 - g.width,
    ]
    return {
      right,
      bottom,
      left: g.left,
      top: g.top,
      fromRight: toTopLeft[0] >= MIN_ROOM || toTopLeft[0] >= toBottomRight[0],
      fromBottom: toTopLeft[1] >= MIN_ROOM || toTopLeft[1] >= toBottomRight[1],
    }
  }
  // The keys always grow towards the top left first (the window moves in when that side is full).
  const keyAnchor = (): Anchor => ({ ...currentAnchor(), fromRight: true, fromBottom: true })

  // Position: the whole bar and the frame strips at the edges (a press that starts on another button or on the size
  // handle is theirs), and the grip button for the keys.
  const drag = useRef<{ id: number; x: number; y: number; left: number; top: number } | null>(null)
  const gripDown = (e: PointerEvent) => {
    if (e.button !== 0) return
    if ((e.target as Element).closest('button:not(.cc2-mini-grip), [role="slider"]')) return
    ;(e.currentTarget as HTMLElement).setPointerCapture(e.pointerId)
    drag.current = {
      id: e.pointerId,
      x: e.clientX,
      y: e.clientY,
      left: geometry.current.left,
      top: geometry.current.top,
    }
  }
  const gripMove = (e: PointerEvent) => {
    const d = drag.current
    if (d && d.id === e.pointerId && buttonLost(e)) return gripUp(e)
    if (d && d.id === e.pointerId) place(d.left + e.clientX - d.x, d.top + e.clientY - d.y)
  }
  const gripUp = (e: PointerEvent) => {
    if (drag.current?.id !== e.pointerId) return
    drag.current = null
  }
  const gripKey = (e: KeyboardEvent) => {
    const step = e.shiftKey ? KEY_STEP_BIG : KEY_STEP
    const move: Record<string, [number, number]> = {
      ArrowLeft: [-step, 0],
      ArrowRight: [step, 0],
      ArrowUp: [0, -step],
      ArrowDown: [0, step],
    }
    const by = move[e.key]
    if (!by) return
    e.preventDefault()
    place(geometry.current.left + by[0], geometry.current.top + by[1])
  }

  // Size: the corner at the top left. Moving it up or to the left makes the window larger (the other way round where
  // that side is full, see above). The width follows the longer of the horizontal and the vertical move, the shape stays 16:9.
  const stretch = useRef<({ id: number; x: number; y: number; width: number } & Anchor) | null>(null)
  const cornerDown = (e: PointerEvent) => {
    if (e.button !== 0) return
    ;(e.currentTarget as HTMLElement).setPointerCapture(e.pointerId)
    stretch.current = { id: e.pointerId, x: e.clientX, y: e.clientY, width: geometry.current.width, ...currentAnchor() }
  }
  const cornerMove = (e: PointerEvent) => {
    const s = stretch.current
    if (!s || s.id !== e.pointerId) return
    if (buttonLost(e)) return cornerUp(e)
    // The growth is the pointer's move along the axes where the handle follows it (towards the top or the left); where
    // both of those sides are full, along both axes the other way. The larger growth wins, so a move in a free direction
    // always grows and never shrinks the window, and the handle stays under the pointer.
    const growth = []
    if (s.fromRight) growth.push(s.x - e.clientX)
    if (s.fromBottom) growth.push(((s.y - e.clientY) * 16) / 9)
    if (growth.length === 0) growth.push(e.clientX - s.x, ((e.clientY - s.y) * 16) / 9)
    resize(s.width + Math.max(...growth), s, true)
  }
  const cornerUp = (e: PointerEvent) => {
    if (stretch.current?.id !== e.pointerId) return
    stretch.current = null
  }
  const cornerKey = (e: KeyboardEvent) => {
    const step = e.shiftKey ? KEY_STEP_BIG : KEY_STEP
    const current = geometry.current.width
    const next =
      e.key === 'ArrowRight' || e.key === 'ArrowUp'
        ? current + step
        : e.key === 'ArrowLeft' || e.key === 'ArrowDown'
          ? current - step
          : e.key === 'Home'
            ? limits.min
            : e.key === 'End'
              ? limits.max
              : null
    if (next === null) return
    e.preventDefault()
    resize(next, keyAnchor())
  }

  // Zoom: digital, in the browser. The picture the camera sends is only magnified; the camera service sees nothing.
  const view = useRef<HTMLDivElement>(null)
  const setView = (nextZoom: number, tx: number, ty: number) => {
    const g = geometry.current
    const z = clampZoom(nextZoom)
    miniplayer.set({ zoom: z, panX: clampPan(tx, g.width, z), panY: clampPan(ty, g.frameHeight, z) })
  }
  const resetZoom = () => miniplayer.set({ zoom: 1, panX: 0, panY: 0 })
  // Zoom to `next`, keeping the point (px, py), measured from the picture's centre, where it is.
  const zoomAt = (next: number, px: number, py: number) => {
    const s = miniplayer.get()
    const z = clampZoom(next)
    const ratio = z / s.zoom
    setView(z, px - (px - s.panX) * ratio, py - (py - s.panY) * ratio)
  }
  const step = (direction: 1 | -1) => {
    const current = miniplayer.get().zoom
    const next =
      direction > 0
        ? (MINI_ZOOM_STEPS.find(v => v > current + 1e-6) ?? current)
        : ([...MINI_ZOOM_STEPS].reverse().find(v => v < current - 1e-6) ?? current)
    zoomAt(next, 0, 0)
  }

  const pointers = useRef(new Map<number, Pointer>())
  const gesture = useRef<Gesture | null>(null)
  const moved = useRef(false)
  const lastTap = useRef({ at: 0, x: 0, y: 0 })
  const centre = () => {
    const r = view.current?.getBoundingClientRect()
    return r ? { x: r.left + r.width / 2, y: r.top + r.height / 2 } : { x: 0, y: 0 }
  }
  const begin = () => {
    const s = miniplayer.get()
    const list = [...pointers.current.values()]
    const c = centre()
    if (list.length === 1) gesture.current = { kind: 'pan', x: list[0].x, y: list[0].y, tx: s.panX, ty: s.panY }
    else if (list.length === 2)
      gesture.current = {
        kind: 'pinch',
        distance: Math.hypot(list[0].x - list[1].x, list[0].y - list[1].y) || 1,
        zoom: s.zoom,
        mx: (list[0].x + list[1].x) / 2 - c.x,
        my: (list[0].y + list[1].y) / 2 - c.y,
        tx: s.panX,
        ty: s.panY,
      }
    else gesture.current = null
  }
  const viewDown = (e: PointerEvent) => {
    if ((e.target as Element).closest('button, [role="slider"]')) return
    if (e.pointerType === 'mouse' && e.button !== 0) return
    view.current?.setPointerCapture(e.pointerId)
    pointers.current.set(e.pointerId, { x: e.clientX, y: e.clientY })
    if (pointers.current.size === 1) moved.current = false
    begin()
  }
  const viewMove = (e: PointerEvent) => {
    if (!pointers.current.has(e.pointerId)) return
    if (buttonLost(e)) return viewUp(e)
    pointers.current.set(e.pointerId, { x: e.clientX, y: e.clientY })
    const g = gesture.current
    const list = [...pointers.current.values()]
    if (g?.kind === 'pan' && list.length === 1) {
      const dx = list[0].x - g.x
      const dy = list[0].y - g.y
      if (Math.hypot(dx, dy) > 6) moved.current = true
      if (miniplayer.get().zoom > 1) setView(miniplayer.get().zoom, g.tx + dx, g.ty + dy)
    } else if (g?.kind === 'pinch' && list.length === 2) {
      moved.current = true
      const c = centre()
      const distance = Math.hypot(list[0].x - list[1].x, list[0].y - list[1].y)
      const z = clampZoom((g.zoom * distance) / g.distance)
      // The point between the fingers at the start stays between the fingers.
      const px = (g.mx - g.tx) / g.zoom
      const py = (g.my - g.ty) / g.zoom
      const mx = (list[0].x + list[1].x) / 2 - c.x
      const my = (list[0].y + list[1].y) / 2 - c.y
      setView(z, mx - z * px, my - z * py)
    }
  }
  const viewUp = (e: PointerEvent) => {
    if (!pointers.current.delete(e.pointerId)) return
    if (pointers.current.size === 0 && e.type === 'pointerup' && e.pointerType === 'touch' && !moved.current) {
      // A double tap resets the zoom. The browser's own double-click is not used: on a phone it also means "zoom the page".
      const now = performance.now()
      const last = lastTap.current
      if (now - last.at < 300 && Math.hypot(e.clientX - last.x, e.clientY - last.y) < 30) {
        resetZoom()
        lastTap.current = { at: 0, x: 0, y: 0 }
      } else lastTap.current = { at: now, x: e.clientX, y: e.clientY }
    }
    begin()
  }
  const viewKey = (e: KeyboardEvent) => {
    if (e.target !== e.currentTarget || e.ctrlKey || e.metaKey || e.altKey) return
    const s = miniplayer.get()
    const by = e.shiftKey ? KEY_STEP_BIG : KEY_STEP
    if (e.key === '+' || e.key === '=') step(1)
    else if (e.key === '-' || e.key === '_') step(-1)
    else if (e.key === '0') resetZoom()
    else if (s.zoom > 1 && e.key === 'ArrowLeft') setView(s.zoom, s.panX + by, s.panY)
    else if (s.zoom > 1 && e.key === 'ArrowRight') setView(s.zoom, s.panX - by, s.panY)
    else if (s.zoom > 1 && e.key === 'ArrowUp') setView(s.zoom, s.panX, s.panY + by)
    else if (s.zoom > 1 && e.key === 'ArrowDown') setView(s.zoom, s.panX, s.panY - by)
    else return
    e.preventDefault()
  }
  // The wheel zooms at the pointer. A listener that may cancel the scrolling, attached to the picture only.
  useEffect(() => {
    const element = view.current
    if (!element) return
    const wheel = (e: WheelEvent) => {
      if ((e.target as Element).closest('button, [role="slider"]')) return
      e.preventDefault()
      const c = centre()
      zoomAt(miniplayer.get().zoom * Math.exp(-e.deltaY * 0.002), e.clientX - c.x, e.clientY - c.y)
    }
    element.addEventListener('wheel', wheel, { passive: false })
    return () => element.removeEventListener('wheel', wheel)
  }, []) // the handler only reads refs and the store, so it is attached once and not on every pointer step

  // Closing on purpose (the X button): the focus goes back to the camera card's Mini player button when the page
  // has one, instead of falling to the page's start. Closing because another viewer took the camera leaves the focus alone.
  const region = useRef<HTMLElement>(null)
  const closeByUser = () => {
    const hadFocus = region.current?.contains(document.activeElement)
    closeMiniplayer()
    if (hadFocus) document.querySelector<HTMLElement>('[data-miniplayer-open]')?.focus()
  }

  // Said aloud by screen readers while the picture is not there yet (the visible hint inside the picture is not announced).
  const announcement = (control: StreamControl) =>
    !control.on || control.ready
      ? ''
      : t(
          control.gaveUp
            ? 'common.camera_gave_up'
            : control.failed
              ? 'common.camera_unavailable'
              : 'common.waiting_for_camera_stream'
        )

  const layout = (frame: VNode, control: StreamControl) => (
    <>
      <div
        class="cc2-mini-bar"
        onPointerDown={gripDown}
        onPointerMove={gripMove}
        onPointerUp={gripUp}
        onPointerCancel={gripUp}
        onLostPointerCapture={gripUp}
      >
        <div
          role="slider"
          tabIndex={0}
          class="cc2-mini-size"
          aria-label={t('common.miniplayer_size')}
          aria-valuemin={limits.min}
          aria-valuemax={limits.max}
          aria-valuenow={width}
          aria-valuetext={tpl('common.miniplayer_size_value', { width })}
          onPointerDown={cornerDown}
          onPointerMove={cornerMove}
          onPointerUp={cornerUp}
          onPointerCancel={cornerUp}
          onLostPointerCapture={cornerUp}
          onKeyDown={cornerKey}
        >
          <svg viewBox="0 0 22 22" aria-hidden="true">
            <path d="M4 18 18 4M4 11 11 4" />
          </svg>
        </div>
        <button
          type="button"
          class="cc2-mini-grip"
          aria-label={t('common.miniplayer_move')}
          title={t('common.miniplayer_move')}
          aria-keyshortcuts="ArrowLeft ArrowRight ArrowUp ArrowDown Shift+ArrowLeft Shift+ArrowRight Shift+ArrowUp Shift+ArrowDown"
          onKeyDown={gripKey}
        >
          <GripVertical size={18} aria-hidden="true" />
        </button>
        <button
          type="button"
          class="cc2-mini-play"
          aria-label={t(control.on ? 'common.camera_stop' : 'common.camera_start')}
          title={t(control.on ? 'common.camera_stop' : 'common.camera_start')}
          onClick={control.toggle}
        >
          <span>{control.on ? <Pause size={14} aria-hidden="true" /> : <Play size={14} aria-hidden="true" />}</span>
        </button>
        <span class="cc2-mini-zoombar">
          <button
            type="button"
            class="cc2-mini-zoombutton"
            aria-label={t('common.miniplayer_zoom_out')}
            aria-disabled={zoom <= 1}
            title={`${t('common.miniplayer_zoom_out')} (−)`}
            onClick={() => step(-1)}
          >
            <Minus size={16} aria-hidden="true" />
          </button>
          <span class="cc2-mini-zoomlabel" data-zoom>
            {zoomText(zoom)}
          </span>
          <button
            type="button"
            class="cc2-mini-zoombutton"
            aria-label={t(zoom >= 2 ? 'common.miniplayer_zoom_in_max' : 'common.miniplayer_zoom_in')}
            aria-disabled={zoom >= 2}
            title={`${t('common.miniplayer_zoom_in')} (+)`}
            onClick={() => step(1)}
          >
            <Plus size={16} aria-hidden="true" />
          </button>
          <span class="cc2-mini-reset-slot">
            {zoom > 1 && (
              <button
                type="button"
                class="cc2-mini-zoombutton cc2-mini-reset"
                aria-label={t('common.miniplayer_zoom_reset')}
                title={`${t('common.miniplayer_zoom_reset')} (0)`}
                onClick={resetZoom}
              >
                1×
              </button>
            )}
          </span>
        </span>
        <button
          type="button"
          class="cc2-mini-close"
          aria-label={t('common.miniplayer_close')}
          title={t('common.miniplayer_close')}
          onClick={closeByUser}
        >
          <span>
            <X size={14} aria-hidden="true" />
          </span>
        </button>
      </div>
      <div
        ref={view}
        role="application"
        // biome-ignore lint/a11y/noNoninteractiveTabindex: the picture takes the zoom keys (+ - 0, arrows) while it has the focus
        tabIndex={0}
        aria-label={tpl('common.miniplayer_view', { zoom: zoomText(zoom) })}
        class="cc2-mini-video"
        style={{ '--mz': zoom, '--mtx': `${panX}px`, '--mty': `${panY}px` }}
        onPointerDown={viewDown}
        onPointerMove={viewMove}
        onPointerUp={viewUp}
        onPointerCancel={viewUp}
        onLostPointerCapture={viewUp}
        onDragStart={e => e.preventDefault()} // dragging the picture pans it; the browser must not start dragging the image
        onKeyDown={viewKey}
        onDblClick={e => {
          if (!(e.target as Element).closest('button, [role="slider"]')) resetZoom()
        }}
      >
        {frame}
        <span class="sr-only" role="status">
          {announcement(control)}
        </span>
        {control.ready && (
          <span class="cc2-mini-badge cc2-mini-live">
            <Dot /> {t('common.live')}
          </span>
        )}
        {zoom > 1 && (
          <span class="cc2-mini-badge cc2-mini-zoom" data-zoom>
            {zoomText(zoom)}
          </span>
        )}
      </div>
      {/* The frame: strips at the left, right and bottom edge that move the window like the bar does. */}
      {(['l', 'r', 'b'] as const).map(edge => (
        <span
          key={edge}
          aria-hidden="true"
          class={`cc2-mini-edge cc2-mini-edge-${edge}`}
          onPointerDown={gripDown}
          onPointerMove={gripMove}
          onPointerUp={gripUp}
          onPointerCancel={gripUp}
          onLostPointerCapture={gripUp}
        />
      ))}
    </>
  )

  return (
    <section
      ref={region}
      aria-label={t('common.miniplayer_region')}
      class="cc2-miniplayer"
      style={{ left: `${left}px`, top: `${top}px`, width: `${width}px` }}
    >
      <CameraStream standalone bare onTakenOver={closeMiniplayer} layout={layout} />
    </section>
  )
}
