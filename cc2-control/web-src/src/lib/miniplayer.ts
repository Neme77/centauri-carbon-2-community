import { store } from './store'

// The mini player: the live camera in a small window that floats over every page. Its size is
// remembered per browser (they depend on the screen, so they are not part of the printer's preferences); whether
// it is open is not, because the camera must never start by itself after a reload.
const KEY = 'cc2-miniplayer'

export const MINI_BAR = 44 // height of the control bar above the 16:9 picture
export const MINI_MARGIN = 8 // distance to the edges of the window
export const MINI_DEFAULT_WIDTH = 360
export const MINI_DEFAULT_WIDTH_PHONE = 260
export const MINI_ZOOM_STEPS = [1, 1.5, 2]
export const MINI_ZOOM_MAX = 2 // digital zoom only magnifies the camera service's stream, so more than 2x would just look blurry

// Desktop: 240 px up to 640 px, at most half the window. Phone (narrow window): 160 px up to the window width minus 24 px,
// because half of a phone screen is smaller than the desktop minimum.
export const miniWidthLimits = (viewport: number) =>
  viewport < 600
    ? { min: 160, max: Math.max(160, viewport - 24) }
    : { min: 240, max: Math.max(240, Math.min(640, Math.floor(viewport / 2))) }

export const miniDefaultWidth = (viewport: number) => (viewport < 600 ? MINI_DEFAULT_WIDTH_PHONE : MINI_DEFAULT_WIDTH)

export const clampMiniWidth = (width: number, viewport: number) => {
  const { min, max } = miniWidthLimits(viewport)
  return Math.round(Math.min(max, Math.max(min, width)))
}

export const miniHeight = (width: number) => Math.round((width * 9) / 16) + MINI_BAR

export const clampZoom = (zoom: number) => Math.min(MINI_ZOOM_MAX, Math.max(1, zoom))

// How far the picture may be shifted at a zoom: half of what sticks out on each side.
export const maxPan = (size: number, zoom: number) => (size * (zoom - 1)) / 2

export const clampPan = (offset: number, size: number, zoom: number) => {
  const limit = maxPan(size, zoom)
  return Math.min(limit, Math.max(-limit, offset))
}

// Nothing about the window is remembered: closing it (X, the button in the card, another viewer taking the camera) and
// reloading the page both end with the standard size at the bottom right on the next opening. A value an earlier version
// stored in this browser is removed once, when the page starts.
try {
  localStorage.removeItem(KEY)
} catch {}

// Whether the window is open is a store of its own: it changes rarely, and the App and the camera card read it.
// The geometry below changes with every pointer step of a drag or a zoom, so only the window itself may read it
// (a shared store would re-render the whole app for every step).
export const miniplayerOpen = store({ open: false })

// `w` is the width in px (null: the default for this screen); `x` and `y` are the window's left and top edge as a
// share of the window size while it is open (null: bottom right), so the place still fits after a rotation or a resize.
export const miniplayer = store({
  w: null as number | null,
  x: null as number | null,
  y: null as number | null,
  zoom: 1,
  panX: 0,
  panY: 0,
})

export const openMiniplayer = () => miniplayerOpen.set({ open: true })

// A closed mini player starts again at the standard size, at the bottom right, at 1x showing the whole picture.
export const closeMiniplayer = () => {
  miniplayer.set({ w: null, x: null, y: null, zoom: 1, panX: 0, panY: 0 })
  miniplayerOpen.set({ open: false })
}
