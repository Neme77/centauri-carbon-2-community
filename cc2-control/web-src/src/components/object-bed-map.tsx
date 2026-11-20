import { ChevronLeft, ChevronRight } from 'lucide-preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { t, tpl } from '@/lib/i18n'
import {
  cachedLayerPath,
  fetchLayerPath,
  GCODE_READ_HELD_RETRY_MS,
  gcodeReadHeld,
  splitLayerPaths,
  type LayerPath,
  type LayerPathPolyline,
} from '@/lib/layer-path'
import {
  cachedObjectPreview,
  fetchObjectPreview,
  objectPreviewBusy,
  objectPreviewDrawing,
  objectPreviewLoading,
  objectPreviewMissing,
  type ObjectPreview,
  type ObjectPreviewMissing,
} from '@/lib/object-preview'
import { objects } from '@/lib/objects'
import { BED_SIZE, onBed } from '@/lib/object-layout'
import { printer, view } from '@/lib/state'
import { cn } from '@/lib/utils'

const ticks = [0, 50, 100, 150, 200, 250]
const STORAGE = 'internal'
// Rapid manual stepping requests only the layer it settles on.
const LAYER_SETTLE_MS = 350
// "Busy" means a scan runs or the service rests after one. The rest is at least 250 ms (LAYER_PATH_REST_MIN_MS in
// the service), so the first retry comes after 300 ms and then backs off to 1.5 s, because a scan of a large file
// runs up to the backend's 20 s deadline. 3 short waits plus 14 of 1.5 s give 23.1 s, more than the 22.5 s
// (15 x 1.5 s) this page waited before.
const LAYER_BUSY_RETRY_FIRST_MS = 300
const LAYER_BUSY_RETRY_MAX_MS = 1500
const LAYER_BUSY_RETRIES = 17
const busyRetryDelay = (attempt: number) => Math.min(LAYER_BUSY_RETRY_MAX_MS, LAYER_BUSY_RETRY_FIRST_MS * 2 ** attempt)
// Live never stays on "Loading layer…". After the backend's scan budget, show
// the path we have or say that the answer did not arrive.
const LAYER_LIVE_WAIT_MS = 20000
// A path one layer behind for a moment is the normal load of the next layer;
// name the lag only when it lasts or spans more than one layer.
const LAYER_BEHIND_SHOW_MS = 3000
// A new print reports the previous job's layer until its first layer change, which comes only after heating,
// homing and probing (seen: layer 193 right after a start made Live scan the whole 78.7 MB file at the worst
// moment). A stale value never changes. So while a print is young, Live follows a layer only when it climbs
// from the start (at most one above the last followed layer) or is seen stepping up by one; a jump keeps the
// last layer that fitted. A layer above the job's layer count never counts.
const LAYER_START_GRACE_S = 300
// The read hold (gcodeReadHeld: the printer homes, levels, probes or starts a print) is a 503 too, but no scan runs:
// it can last minutes, so it is asked again every GCODE_READ_HELD_RETRY_MS, never counted as a busy retry and never
// given up while the view is open. The status line says that the view waits for the printer.
const layerPathBusy = (error: unknown) => (error as { status?: number } | null)?.status === 503
// The object preview reads the whole file once in the service's one scan slot. While this page waits for it,
// "busy" is the expected answer to a layer request, however long that read and its rest take.
const layerBusyRetry = (error: unknown, attempt: number) =>
  layerPathBusy(error) && (attempt < LAYER_BUSY_RETRIES || objectPreviewLoading.get().active)
/* The backend gave up on the scan (time or size budget): say so instead of "not available". */
const layerPathLimit = (error: unknown) => {
  const e = error as { status?: number; data?: { error?: unknown } } | null
  return e?.status === 413 && e.data?.error === 'Layer path scan limit reached'
}

const pointsAttr = (path: LayerPathPolyline) => path.map(([x, y]) => `${x},${BED_SIZE - y}`).join(' ')

// The number sits in the middle of the object on the Objects tab. On the layer path it would hide the lines, so it
// goes beside the object: level with its middle, its right edge a little left of the leftmost outline point.
const LAYER_BADGE_GAP_MM = 1.5

function markerPoint(
  center: [number, number] | null,
  polygon: [number, number][] | null,
  beside: boolean
): [number, number] | null {
  if (!center) return null
  if (!beside) return center
  const left = polygon?.length ? Math.min(...polygon.map(p => p[0])) : center[0] - 12
  return [Math.max(0, left - LAYER_BADGE_GAP_MM), center[1]]
}

const pointInShape = (p: [number, number], shape: [number, number][]) => {
  let inside = false
  for (let i = 0, j = shape.length - 1; i < shape.length; j = i++) {
    const [xi, yi] = shape[i]
    const [xj, yj] = shape[j]
    if (yi > p[1] !== yj > p[1] && p[0] < ((xj - xi) * (p[1] - yi)) / (yj - yi) + xi) inside = !inside
  }
  return inside
}
// A line belongs to an object when most of its points lie in the object's outline.
const inAnyShape = (line: LayerPathPolyline, shapes: [number, number][][]) =>
  line.length > 0 && shapes.some(shape => line.filter(p => pointInShape(p, shape)).length * 2 >= line.length)

const formatZ = (z: number) => (Math.round(z * 100) / 100).toFixed(2)

// The printed part fades in from its start to the nozzle so the print direction reads
// at a glance; a small arrow stops just short of the nozzle. Eight steps keep the DOM small.
const FADE_STEPS = 8
function fadedChunks(paths: LayerPathPolyline[]) {
  const len = (a: [number, number], b: [number, number]) => Math.hypot(b[0] - a[0], b[1] - a[1])
  const total = paths.reduce((sum, path) => sum + path.slice(1).reduce((s2, p, i) => s2 + len(path[i], p), 0), 0)
  const chunks: { points: LayerPathPolyline; step: number }[] = []
  let done = 0
  for (const path of paths) {
    if (path.length < 2) continue
    let current: LayerPathPolyline = [path[0]]
    let step = total > 0 ? Math.min(FADE_STEPS - 1, Math.floor((done / total) * FADE_STEPS)) : FADE_STEPS - 1
    for (let i = 1; i < path.length; i++) {
      done += len(path[i - 1], path[i])
      current.push(path[i])
      const next = total > 0 ? Math.min(FADE_STEPS - 1, Math.floor((done / total) * FADE_STEPS)) : FADE_STEPS - 1
      if (next !== step && i < path.length - 1) {
        chunks.push({ points: current, step })
        current = [path[i]]
        step = next
      }
    }
    chunks.push({ points: current, step })
  }
  return chunks
}

function LayerPolylines({
  paths,
  kind,
}: {
  paths: LayerPathPolyline[]
  kind: 'prev' | 'printed' | 'remain' | 'skipped'
}) {
  if (kind === 'printed') {
    const chunks = fadedChunks(paths)
    return (
      <>
        {chunks.map((chunk, i) => (
          <polyline
            key={`printed-${i}`}
            data-layer-path="printed"
            points={pointsAttr(chunk.points)}
            class="cc2-layer-path cc2-layer-path-printed"
            style={{ opacity: 0.55 + (0.45 * (chunk.step + 1)) / FADE_STEPS }}
            marker-end={i === chunks.length - 1 ? 'url(#cc2-layer-path-direction)' : undefined}
          />
        ))}
      </>
    )
  }
  return (
    <>
      {paths.map((path, i) =>
        path.length > 1 ? (
          <polyline
            key={`${kind}-${i}`}
            data-layer-path={kind}
            points={pointsAttr(path)}
            class={cn(
              'cc2-layer-path',
              kind === 'prev' && 'cc2-layer-path-prev',
              kind === 'remain' && 'cc2-layer-path-remain',
              kind === 'skipped' && 'cc2-layer-path-skipped'
            )}
          />
        ) : null
      )}
    </>
  )
}

const PREVIEW_MISSING_TEXT = {
  too_large: 'job.object_preview_too_large',
  limit: 'job.object_preview_limit',
  starting: 'job.object_preview_starting',
  missing: 'job.object_preview_unavailable',
} as const satisfies Record<ObjectPreviewMissing, string>

// The rough shape of the selected object, read from the G-code: outer walls only, at most 24 layers.
// The service reads the file once for all objects and remembers them, so another object comes at once.
function ObjectPreviewPanel({ file, object, label }: { file: string; object: string; label: string }) {
  const [preview, setPreview] = useState<ObjectPreview | null>(null)
  const [missing, setMissing] = useState<false | ObjectPreviewMissing>(false)
  const [held, setHeld] = useState(false)
  useEffect(() => {
    const hit = cachedObjectPreview(STORAGE, file, object)
    setPreview(hit || null)
    setMissing(false)
    setHeld(false)
    if (hit) return
    const ac = new AbortController()
    let timer: ReturnType<typeof setTimeout> | undefined
    const wait = (ms: number) => new Promise<void>(resolve => (timer = setTimeout(resolve, ms)))
    objectPreviewLoading.set({ active: true })
    const load = async () => {
      try {
        for (let attempt = 0; ; ) {
          try {
            const loaded = await fetchObjectPreview(STORAGE, file, object, ac.signal)
            if (!ac.signal.aborted) {
              setHeld(false)
              setPreview(loaded)
            }
            return
          } catch (error) {
            if (ac.signal.aborted) return
            const heldNow = gcodeReadHeld(error)
            setHeld(heldNow)
            if (heldNow) {
              await wait(GCODE_READ_HELD_RETRY_MS)
              if (ac.signal.aborted) return
              continue
            }
            if (!objectPreviewBusy(error) || attempt >= LAYER_BUSY_RETRIES) {
              setMissing(objectPreviewMissing(error))
              return
            }
            await wait(busyRetryDelay(attempt))
            attempt++
            if (ac.signal.aborted) return
          }
        }
      } finally {
        if (!ac.signal.aborted) objectPreviewLoading.set({ active: false })
      }
    }
    void load()
    return () => {
      ac.abort()
      clearTimeout(timer)
      objectPreviewLoading.set({ active: false })
    }
  }, [file, object])
  const drawing = preview ? objectPreviewDrawing(preview) : null
  return (
    <figure class="m-0 mb-3 rounded-md border border-edge bg-field p-3" data-object-preview={object}>
      <figcaption class="mb-2 text-xs text-muted">
        {t('job.object_preview')}: <b class="text-fg [overflow-wrap:anywhere]">{label}</b>
      </figcaption>
      <div class="cc2-object-preview-stage" aria-busy={!preview && !missing ? 'true' : undefined}>
        {drawing ? (
          <svg
            viewBox={drawing.viewBox}
            class="h-full w-full"
            role="img"
            aria-label={`${t('job.object_preview')}: ${label}`}
            data-object-preview-rings={preview?.rings.length}
          >
            {drawing.lines.map((line, i) => (
              <polyline
                key={i}
                points={line.points}
                class="cc2-object-preview-line"
                style={{ opacity: 0.5 + 0.5 * line.height }}
              />
            ))}
          </svg>
        ) : (
          <p
            class="text-center text-xs text-muted"
            role="status"
            data-object-preview-status={missing || (held ? 'held' : 'loading')}
          >
            {t(
              missing
                ? PREVIEW_MISSING_TEXT[missing]
                : preview
                  ? 'job.object_preview_unavailable'
                  : held
                    ? 'job.gcode_read_held'
                    : 'job.object_preview_loading'
            )}
          </p>
        )}
      </div>
      {drawing && preview && preview.layers > 0 && (
        <p class="mt-2 text-xs text-muted" data-object-preview-note>
          {tpl('job.object_preview_note', { n: preview.rings.length, total: preview.layers })}
        </p>
      )}
    </figure>
  )
}

// Map prototype: Nick Plateringen-Paul. Single-view refinement: DamiBFryta.
// Both sizes read the same model; selection never changes the original G-code ID.
export const ObjectBedMap = ({
  embedded,
  idle,
  onExclude,
  live,
}: {
  /** The large map with its tabs shown on the page itself, without a dialog. */
  embedded?: boolean
  /** No print: the tabs are greyed out and Objects stays selected. */
  idle?: boolean
  onExclude: () => void
  live: boolean
}) => {
  const o = objects.use()
  const printerState = printer.use()
  const job = view(printerState.data)
  const selected = o.entries.find(e => e.id === o.selected)
  const missing = o.entries.filter(e => !onBed(e.center)).length
  const printerTotal = typeof job.total === 'number' ? job.total : 0
  const file = job.rawFilename
  const reportedLayer = Number(job.layer) || 0
  const elapsed = Number(printerState.data?.print?.duration)
  const jobKey = job.active ? `${file}|${printerState.data?.print?.uuid || ''}` : ''
  // Without a print duration, a job that this page saw starting counts as young.
  const lastJobKey = useRef<string | null>(null)
  const jobSeenStarting = useRef<{ key: string; at: number } | null>(null)
  if (lastJobKey.current !== null && jobKey && jobKey !== lastJobKey.current)
    jobSeenStarting.current = { key: jobKey, at: Date.now() }
  lastJobKey.current = jobKey
  const young = Number.isFinite(elapsed)
    ? elapsed >= 0 && elapsed < LAYER_START_GRACE_S
    : jobSeenStarting.current?.key === jobKey && Date.now() - jobSeenStarting.current.at < LAYER_START_GRACE_S * 1000
  const climbed = useRef({ key: '', max: 0 })
  if (climbed.current.key !== jobKey) climbed.current = { key: jobKey, max: 0 }
  let printerLayer = reportedLayer < 1 || (printerTotal > 0 && reportedLayer > printerTotal) ? 0 : reportedLayer
  const lastReported = useRef({ key: '', layer: -1 })
  const steppedUp =
    lastReported.current.key === jobKey &&
    lastReported.current.layer >= 1 &&
    reportedLayer === lastReported.current.layer + 1
  lastReported.current = { key: jobKey, layer: reportedLayer }
  if (printerLayer > 0 && young && printerLayer > climbed.current.max + 1 && !steppedUp)
    printerLayer = climbed.current.max
  if (printerLayer > climbed.current.max) climbed.current.max = printerLayer
  const [tabChoice, setTab] = useState<'objects' | 'layer'>('objects')
  const tab = idle ? 'objects' : tabChoice
  useEffect(() => {
    if (idle) setTab('objects')
  }, [idle])
  const [liveFollow, setLiveFollow] = useState(true)
  const [manualLayer, setManualLayer] = useState(1)
  const [path, setPath] = useState<LayerPath | null>(null)
  const [prevPath, setPrevPath] = useState<LayerPath | null>(null)
  const [loading, setLoading] = useState(false)
  const [timedOut, setTimedOut] = useState(false)
  const [unavailable, setUnavailable] = useState<false | 'limit' | 'missing'>(false)
  const [held, setHeld] = useState(false)
  // The layer path loads only while its tab is open; leaving the tab drops what was loaded, so the next visit starts clean.
  useEffect(() => {
    if (tab === 'layer') return
    setPath(null)
    setPrevPath(null)
    setLoading(false)
    setTimedOut(false)
    setUnavailable(false)
    setHeld(false)
  }, [tab])
  const viewLayer = liveFollow ? printerLayer : manualLayer
  const printerLayerRef = useRef(printerLayer)
  printerLayerRef.current = printerLayer
  // Keep the file's layer count while a stepped-to layer loads, so the status
  // always shows the layer number and the total.
  const knownTotal = useRef<{ file: string; total: number } | null>(null)
  if (file && path?.total_layers) knownTotal.current = { file, total: path.total_layers }
  const totalLayers =
    path?.total_layers || printerTotal || (knownTotal.current?.file === file ? knownTotal.current.total : 0)
  const expanded = Boolean(embedded)
  const layerTab = expanded && tab === 'layer'
  const nozzleRaw = printerState.data?.motion
  const nozzle: [number, number] | null =
    typeof nozzleRaw?.x === 'number' && typeof nozzleRaw?.y === 'number' ? [nozzleRaw.x, nozzleRaw.y] : null
  const nozzleOnBed = onBed(nozzle)

  // Manual paging still settles, aborts the layer it left, and draws only the
  // layer it stopped on. Live is a separate effect: an in-flight request is
  // allowed to finish, and the path stays up while a newer layer is loading.
  useEffect(() => {
    if (!expanded || tab !== 'layer' || liveFollow) return
    if (!file || manualLayer < 1) {
      setPath(null)
      setPrevPath(null)
      setLoading(false)
      setTimedOut(false)
      setUnavailable('missing')
      return
    }
    const ac = new AbortController()
    let timer: ReturnType<typeof setTimeout> | undefined
    const wait = (ms: number) => new Promise<void>(resolve => (timer = setTimeout(resolve, ms)))
    const hit = cachedLayerPath(STORAGE, file, manualLayer)
    const prevHit = manualLayer > 1 ? cachedLayerPath(STORAGE, file, manualLayer - 1) : undefined
    // Stepping keeps the last drawn path (dimmed) until the new layer arrives, so the map does not go blank.
    setPath(current => hit || (current && current.file === file ? current : null))
    setPrevPath(current => prevHit || (hit ? null : current))
    setLoading(!hit)
    setTimedOut(false)
    setUnavailable(false)
    setHeld(false)
    const load = async () => {
      try {
        if (!hit) {
          await wait(LAYER_SETTLE_MS)
          let current: LayerPath | null = null
          for (let attempt = 0; !current; ) {
            if (ac.signal.aborted) return
            try {
              current = await fetchLayerPath(STORAGE, file, manualLayer, ac.signal)
            } catch (error) {
              if (ac.signal.aborted) return
              const heldNow = gcodeReadHeld(error)
              setHeld(heldNow)
              if (heldNow) {
                await wait(GCODE_READ_HELD_RETRY_MS)
                continue
              }
              if (!layerBusyRetry(error, attempt)) throw error
              await wait(busyRetryDelay(attempt))
              attempt++
            }
          }
          if (ac.signal.aborted) return
          setHeld(false)
          setPath(current)
          setLoading(false)
        }
        if (manualLayer > 1 && !prevHit) {
          const previous = await fetchLayerPath(STORAGE, file, manualLayer - 1, ac.signal).catch(() => null)
          if (!ac.signal.aborted) setPrevPath(previous)
        }
      } catch (error) {
        if (ac.signal.aborted) return
        setHeld(false)
        setPath(null)
        setPrevPath(null)
        setLoading(false)
        setUnavailable(layerPathLimit(error) ? 'limit' : 'missing')
      }
    }
    void load()
    return () => {
      ac.abort()
      clearTimeout(timer)
    }
  }, [expanded, tab, file, manualLayer, liveFollow])

  // Live follows the printer without cancelling a scan that is already running.
  // The last path stays on screen (labelled with both layer numbers when it
  // lags). When it finishes, the newest layer is requested. The effect does not
  // depend on the printer layer, so a fast layer change does not restart it.
  useEffect(() => {
    if (!expanded || tab !== 'layer' || !liveFollow) return
    if (!file) {
      setPath(null)
      setPrevPath(null)
      setLoading(false)
      setTimedOut(false)
      setUnavailable('missing')
      return
    }
    const ac = new AbortController()
    let timer: ReturnType<typeof setTimeout> | undefined
    let slow: ReturnType<typeof setTimeout> | undefined
    let cancelled = false
    // A different file's path is not a result for this one.
    setPath(current => (current && current.file === file ? current : null))
    setPrevPath(current => (current && current.file === file ? current : null))
    const wait = (ms: number) => new Promise<void>(resolve => (timer = setTimeout(resolve, ms)))
    const load = async () => {
      let havePath = false
      let prevTried = 0
      while (!cancelled) {
        const target = printerLayerRef.current
        if (target < 1) {
          havePath = false
          setPath(null)
          setPrevPath(null)
          setLoading(false)
          setTimedOut(false)
          setUnavailable(false)
          setHeld(false)
          while (!cancelled && printerLayerRef.current < 1) await wait(100)
          continue
        }
        const hit = cachedLayerPath(STORAGE, file, target)
        if (hit) {
          setPath(hit)
          setPrevPath(target > 1 ? cachedLayerPath(STORAGE, file, target - 1) || null : null)
          setLoading(false)
          setTimedOut(false)
          setUnavailable(false)
          setHeld(false)
          havePath = true
          if (target > 1 && !cachedLayerPath(STORAGE, file, target - 1) && prevTried !== target) {
            prevTried = target
            const previous = await fetchLayerPath(STORAGE, file, target - 1, ac.signal).catch(() => null)
            if (cancelled) return
            setPrevPath(previous)
          }
          while (!cancelled && printerLayerRef.current === target) await wait(100)
          continue
        }
        setUnavailable(false)
        if (!havePath) setLoading(true)
        const since = Date.now()
        clearTimeout(slow)
        slow = setTimeout(
          () => {
            if (!cancelled && !havePath) {
              setTimedOut(true)
              setLoading(false)
            }
          },
          Math.max(0, LAYER_LIVE_WAIT_MS - (Date.now() - since))
        )
        let current: LayerPath | null = null
        let failed: false | 'limit' | 'missing' = false
        for (let attempt = 0; !current && !failed; ) {
          if (cancelled) return
          try {
            current = await fetchLayerPath(STORAGE, file, target, ac.signal)
          } catch (error) {
            if (cancelled || ac.signal.aborted) return
            const heldNow = gcodeReadHeld(error)
            setHeld(heldNow)
            if (heldNow || layerBusyRetry(error, attempt)) {
              // A busy reply has finished. Ask for the printer's layer, not the one it left.
              if (printerLayerRef.current !== target) break
              await wait(heldNow ? GCODE_READ_HELD_RETRY_MS : busyRetryDelay(attempt))
              if (!heldNow) attempt++
              continue
            }
            failed = layerPathLimit(error) ? 'limit' : 'missing'
          }
        }
        clearTimeout(slow)
        if (cancelled) return
        if (current || failed) setHeld(false)
        if (failed) {
          setLoading(false)
          setTimedOut(false)
          if (!havePath) {
            setPath(null)
            setPrevPath(null)
          }
          setUnavailable(failed)
          const stuck = target
          while (!cancelled && printerLayerRef.current === stuck) await wait(100)
          if (!cancelled) setUnavailable(false)
          continue
        }
        if (!current) continue
        setPath(current)
        setLoading(false)
        setTimedOut(false)
        setUnavailable(false)
        havePath = true
        if (current.layer > 1 && printerLayerRef.current === current.layer && prevTried !== current.layer) {
          prevTried = current.layer
          const prevHit = cachedLayerPath(STORAGE, file, current.layer - 1)
          if (prevHit) setPrevPath(prevHit)
          else {
            const previous = await fetchLayerPath(STORAGE, file, current.layer - 1, ac.signal).catch(() => null)
            if (!cancelled) setPrevPath(previous)
          }
        }
      }
    }
    void load()
    return () => {
      cancelled = true
      ac.abort()
      clearTimeout(timer)
      clearTimeout(slow)
    }
  }, [expanded, tab, file, liveFollow])

  const openLayer = () => setTab('layer')
  const followLive = () => {
    openLayer()
    setLiveFollow(true)
  }
  // A second click on the selected object clears the selection again.
  const pick = (id: string) => objects.set({ selected: o.selected === id ? '' : id })
  const stepLayer = (delta: number) => {
    // From layer 0 (print preparing) the first step lands on layer 1.
    const from = viewLayer >= 1 ? viewLayer : 0
    const next = Math.max(1, totalLayers > 0 ? Math.min(totalLayers, from + delta) : from + delta)
    openLayer()
    setLiveFollow(false)
    setManualLayer(next)
  }

  const splitMode = liveFollow
    ? 'split'
    : printerLayer > 0 && viewLayer < printerLayer
      ? 'printed'
      : printerLayer > 0 && viewLayer > printerLayer
        ? 'remaining'
        : 'split'
  // The path of an excluded object is not printed: draw it pale instead of as part of the path.
  const excludedShapes = o.entries
    .filter(e => e.polygon && o.excluded.includes(e.id))
    .map(e => e.polygon as [number, number][])
  const skipped: LayerPathPolyline[] = []
  const active: LayerPathPolyline[] = []
  for (const line of path?.paths || []) (inAnyShape(line, excludedShapes) ? skipped : active).push(line)
  const { printed, remaining } = splitLayerPaths(active, nozzleOnBed ? nozzle : null, splitMode)
  const zValue = path?.z ?? (liveFollow && typeof nozzleRaw?.z === 'number' ? nozzleRaw.z : null)
  const waitingFirstLayer = layerTab && liveFollow && Boolean(file) && printerLayer < 1
  // Live keeps drawing the last path. When that path is not the printer's layer,
  // name both so a slow scan is not mistaken for the layer being printed.
  const lag = path && printerLayer >= 1 ? printerLayer - path.layer : 0
  const behindNow = layerTab && liveFollow && !waitingFirstLayer && Boolean(path) && printerLayer >= 1 && lag !== 0
  const [behindLong, setBehindLong] = useState(false)
  useEffect(() => {
    setBehindLong(false)
    if (!behindNow) return
    const timer = setTimeout(() => setBehindLong(true), LAYER_BEHIND_SHOW_MS)
    return () => clearTimeout(timer)
  }, [behindNow, path?.layer])
  const behind = behindNow && (behindLong || Math.abs(lag) > 1)
  const showLoading = layerTab && loading && !path && !timedOut
  // Live names the layer whose path is drawn; it changes to the printer's layer once that path is in.
  const shownLayer = liveFollow && path ? path.layer : viewLayer
  // The drawn path belongs to another layer while a stepped-to layer loads: dim it and keep the Z slot.
  const stale = layerTab && Boolean(path) && path?.layer !== shownLayer
  const shownZ = stale ? null : zValue
  const mapLoading = layerTab && loading && !timedOut
  // The Objects tab shows every layer's outline, so it names only the layer being printed.
  const status = !expanded
    ? ''
    : idle
      ? t('job.no_print')
      : !layerTab
        ? printerLayer >= 1 && printerTotal > 0
          ? tpl('job.bed_map_printing_layer', { n: printerLayer, total: printerTotal })
          : ''
        : waitingFirstLayer
          ? t('job.bed_map_waiting_first_layer')
          : held
            ? t('job.gcode_read_held')
            : layerTab && unavailable
              ? t(unavailable === 'limit' ? 'job.bed_map_layer_limit' : 'job.bed_map_layer_unavailable')
              : behind && path
                ? tpl('job.bed_map_layer_behind', { n: path.layer, m: printerLayer })
                : showLoading && !(shownLayer >= 1 && totalLayers > 0)
                  ? t('job.bed_map_loading_layer')
                  : layerTab && timedOut && !path
                    ? t('job.bed_map_layer_timeout')
                    : shownLayer >= 1 && totalLayers > 0
                      ? tpl('job.bed_map_layer_status', {
                          n: shownLayer,
                          total: totalLayers,
                          z: shownZ != null ? formatZ(shownZ) : '…',
                        })
                      : layerTab
                        ? t('job.bed_map_layer_unavailable')
                        : ''

  const content = (
    <div
      class={cn('cc2-object-map', expanded && 'cc2-object-map-expanded', embedded && 'cc2-object-map-embedded')}
      data-bed-map-tab={expanded ? tab : undefined}
    >
      <p class="mb-3 text-xs text-muted">{t('job.bed_map_hint')}</p>
      {expanded && (
        <div class="mb-3 flex flex-wrap items-center gap-2">
          {/* The tabs and Live always use the weight of a selected button: otherwise selecting one makes it wider and moves the controls beside it. */}
          <fieldset class="m-0 flex h-11 min-w-0 overflow-hidden rounded-md border border-edge p-0">
            <legend class="sr-only">{t('job.bed_map_view')}</legend>
            <Button
              class="h-11 min-h-11 rounded-none border-0 px-3.5 font-semibold whitespace-nowrap"
              variant={tab === 'objects' ? 'primary' : 'default'}
              aria-pressed={tab === 'objects'}
              disabled={idle}
              onClick={() => setTab('objects')}
            >
              {t('job.bed_map_objects')}
            </Button>
            <Button
              class="h-11 min-h-11 rounded-none border-0 border-l border-edge px-3.5 font-semibold whitespace-nowrap"
              variant={tab === 'layer' ? 'primary' : 'default'}
              aria-pressed={tab === 'layer'}
              disabled={idle}
              onClick={openLayer}
            >
              {t('job.bed_map_layer_path')}
            </Button>
          </fieldset>
          <div class="flex flex-wrap items-center gap-2">
            <Button
              class="h-11 w-11 min-h-11 p-0"
              aria-label={t('job.bed_map_previous_layer')}
              disabled={!layerTab || viewLayer <= 1 || !file}
              onClick={() => stepLayer(-1)}
            >
              <ChevronLeft size={18} />
            </Button>
            <Button
              class="h-11 w-11 min-h-11 p-0"
              aria-label={t('job.bed_map_next_layer')}
              disabled={!layerTab || !file || (totalLayers > 0 && viewLayer >= totalLayers)}
              onClick={() => stepLayer(1)}
            >
              <ChevronRight size={18} />
            </Button>
            <Button
              class="h-11 min-h-11 px-3.5 font-semibold whitespace-nowrap"
              variant={layerTab && liveFollow ? 'primary' : 'default'}
              aria-pressed={layerTab && liveFollow}
              data-layer-live
              disabled={idle || !file}
              onClick={followLive}
            >
              {t('job.bed_map_live')}
            </Button>
          </div>
          {/* Phones: the status gets its own line with room for two, so the longer catch-up text neither clips nor moves the map. */}
          <p
            class="min-h-8 min-w-0 basis-full text-xs text-muted cc2-md:min-h-4 cc2-md:flex-1 cc2-md:basis-0 cc2-md:whitespace-nowrap"
            data-layer-status
            role="status"
            aria-busy={mapLoading ? 'true' : undefined}
          >
            {status}
            {mapLoading && <span class="cc2-layer-loading-dot" data-layer-loading aria-hidden="true" />}
          </p>
        </div>
      )}
      <div class="cc2-object-map-layout grid min-w-0 gap-3.5 cc2-md:grid-cols-2">
        <div class="min-w-0">
          <div class="cc2-object-map-frame">
            <div class="mb-2 flex flex-wrap items-start justify-between gap-x-3 gap-y-1.5 text-xs text-muted">
              <span class="order-1 shrink-0">
                {BED_SIZE} × {BED_SIZE} mm
              </span>
              <div class="cc2-object-map-legend order-3 flex min-h-9 basis-full flex-wrap content-start justify-center gap-x-4 gap-y-1 cc2-sm:order-2 cc2-sm:min-h-0 cc2-sm:flex-1 cc2-sm:basis-auto">
                <span class="flex items-center gap-1.5">
                  <i class="h-2 w-2 rounded-full bg-cyan" />
                  {t('job.bed_map_selected')}
                </span>
                <span class="flex items-center gap-1.5">
                  <i class="h-2 w-2 rounded-full bg-amber" />
                  {t('job.current_object')}
                </span>
                <span class="flex items-center gap-1.5">
                  <i class="h-2 w-2 rounded-full bg-red" />
                  {t('job.excluded')}
                </span>
                {expanded && (
                  <span class="flex items-center gap-1.5">
                    <i class="cc2-layer-path-swatch" />
                    {t('job.bed_map_layer_legend')}
                  </span>
                )}
                {layerTab && liveFollow && (
                  <span class="flex items-center gap-1.5">
                    <i class="cc2-layer-nozzle-swatch" />
                    {t('job.bed_map_nozzle')}
                  </span>
                )}
              </div>
              <span class="order-2 shrink-0 cc2-sm:order-3">Y ↑ · X →</span>
            </div>
            <div class="cc2-object-map-plate">
              <div class="cc2-object-map-surface" data-bed-map aria-busy={mapLoading ? 'true' : undefined}>
                <svg
                  viewBox={`0 0 ${BED_SIZE} ${BED_SIZE}`}
                  class="pointer-events-none absolute inset-0 h-full w-full"
                  aria-hidden="true"
                >
                  {ticks.map(n => (
                    <path
                      key={n}
                      d={`M ${n} 0 V ${BED_SIZE} M 0 ${BED_SIZE - n} H ${BED_SIZE}`}
                      class="cc2-object-map-grid"
                      fill="none"
                    />
                  ))}
                  {o.entries.map(
                    e =>
                      e.polygon &&
                      onBed(e.center) && (
                        // biome-ignore lint/a11y/noStaticElementInteractions: the object area is a mouse shortcut; the numbered button and the list are the keyboard path
                        <polygon
                          key={e.id}
                          data-bed-outline-id={e.id}
                          data-selected={e.id === o.selected ? 'true' : undefined}
                          data-current={e.id === o.current ? 'true' : undefined}
                          data-excluded={o.excluded.includes(e.id) ? 'true' : undefined}
                          points={e.polygon.map(([x, y]) => `${x},${BED_SIZE - y}`).join(' ')}
                          class={cn(
                            'cc2-object-map-outline',
                            live && !o.excluded.includes(e.id) && 'cc2-object-map-outline-pick'
                          )}
                          onClick={live && !o.excluded.includes(e.id) ? () => pick(e.id) : undefined}
                        />
                      )
                  )}
                  <defs>
                    <marker
                      id="cc2-layer-path-direction"
                      viewBox="0 0 10 10"
                      refX="18"
                      refY="5"
                      markerWidth="5"
                      markerHeight="5"
                      markerUnits="userSpaceOnUse"
                      orient="auto"
                      overflow="visible"
                    >
                      <path d="M0 1 L10 5 L0 9 z" class="cc2-layer-path-direction" />
                    </marker>
                  </defs>
                  <g class={cn(stale && 'cc2-layer-path-stale')}>
                    {layerTab && !unavailable && prevPath && <LayerPolylines paths={prevPath.paths} kind="prev" />}
                    {layerTab && !unavailable && path && (
                      <>
                        <LayerPolylines paths={skipped} kind="skipped" />
                        <LayerPolylines paths={remaining} kind="remain" />
                        <LayerPolylines paths={printed} kind="printed" />
                      </>
                    )}
                  </g>
                </svg>
                {o.entries.map((e, i) => {
                  if (!onBed(e.center)) return null
                  const excluded = o.excluded.includes(e.id)
                  const current = e.id === o.current
                  const chosen = e.id === o.selected
                  const at = markerPoint(e.center, e.polygon, layerTab)
                  if (!at) return null
                  return (
                    <button
                      type="button"
                      key={e.id}
                      data-bed-object-id={e.id}
                      data-current={current ? 'true' : undefined}
                      data-selected={chosen ? 'true' : undefined}
                      data-excluded={excluded ? 'true' : undefined}
                      data-layer-badge={layerTab ? 'true' : undefined}
                      aria-label={`${i + 1}. ${e.label} · X ${e.center[0].toFixed(1)} / Y ${e.center[1].toFixed(1)} mm${current ? ` · ${t('job.current_object')}` : ''}${excluded ? ` · ${t('job.excluded')}` : ''}`}
                      aria-pressed={chosen}
                      title={e.label}
                      disabled={excluded || !live}
                      class="cc2-object-map-marker focus-visible:outline-2 focus-visible:outline-offset-4 focus-visible:outline-cyan"
                      style={{
                        left: `${(at[0] / BED_SIZE) * 100}%`,
                        bottom: `${(at[1] / BED_SIZE) * 100}%`,
                      }}
                      onClick={() => pick(e.id)}
                    >
                      {i + 1}
                    </button>
                  )
                })}
                {layerTab && liveFollow && !unavailable && nozzleOnBed && nozzle && (
                  <span
                    role="img"
                    data-layer-nozzle
                    class="cc2-layer-nozzle-dot"
                    aria-label={t('job.bed_map_nozzle')}
                    style={{
                      left: `${(nozzle[0] / BED_SIZE) * 100}%`,
                      bottom: `${(nozzle[1] / BED_SIZE) * 100}%`,
                    }}
                  />
                )}
                {showLoading && (
                  <div class="cc2-layer-path-status" role="status">
                    {t('job.bed_map_loading_layer')}
                  </div>
                )}
              </div>
            </div>
            <p class="mt-2 text-center text-xs text-muted">{t('job.bed_map_front')}</p>
          </div>
          {missing > 0 && <p class="mt-3 text-xs text-muted">{tpl('job.bed_map_missing', { n: missing })}</p>}
        </div>
        <div class="flex min-w-0 flex-col">
          <p class="mb-2 text-xs text-muted">{t('job.object_position_order')}</p>
          {expanded && layerTab && behind && (
            <p class="mb-2 text-xs text-muted" data-layer-behind-hint>
              {t('job.bed_map_layer_behind_hint')}
            </p>
          )}
          <div class="cc2-object-map-list grid content-start gap-2 overflow-y-auto p-1">
            {o.entries.length === 0 ? (
              <div class="text-muted" role="status">
                <p>{t('job.no_live_object_data')}</p>
                <p class="mt-2 text-xs">
                  {t(
                    o.error
                      ? 'job.object_status_unavailable'
                      : o.has
                        ? 'job.no_labelled_objects_reported_by'
                        : 'job.objects_will_appear_during_a'
                  )}
                </p>
              </div>
            ) : (
              o.entries.map((e, i) => {
                const excluded = o.excluded.includes(e.id)
                const current = e.id === o.current
                return (
                  <Button
                    key={e.id}
                    data-object-id={e.id}
                    data-bed-list-id={e.id}
                    data-current={current ? 'true' : undefined}
                    aria-pressed={e.id === o.selected}
                    title={e.label}
                    variant={e.id === o.selected ? 'active' : 'default'}
                    disabled={excluded || !live}
                    class={cn(
                      'h-auto justify-start whitespace-normal text-left text-xs [overflow-wrap:anywhere]',
                      excluded && 'text-red line-through'
                    )}
                    onClick={() => pick(e.id)}
                  >
                    <span class="shrink-0 font-bold">{i + 1}.</span>
                    <span>
                      {e.label}
                      {excluded ? ` · ${t('job.excluded')}` : ''}
                    </span>
                    {current && !excluded && (
                      <span role="img" class="ml-auto shrink-0 text-amber" aria-label={t('job.current_object')}>
                        ●
                      </span>
                    )}
                  </Button>
                )
              })
            )}
          </div>
          {expanded && selected && file && (
            <div class="mt-3">
              <ObjectPreviewPanel
                key={`${file}\n${selected.id}`}
                file={file}
                object={selected.id}
                label={selected.label}
              />
            </div>
          )}
          <div
            class={cn(
              'mb-3 rounded-md border border-edge bg-field p-3 text-xs [overflow-wrap:anywhere]',
              !(expanded && selected && file) && 'mt-3'
            )}
            data-bed-selection
          >
            {selected ? (
              <>
                <span class="text-muted">{t('job.bed_map_selected')}: </span>
                <b>{selected.label}</b>
                <p class="mt-1 text-muted">
                  {onBed(selected.center)
                    ? `X ${selected.center[0].toFixed(1)} / Y ${selected.center[1].toFixed(1)} mm`
                    : t('job.object_position_unknown')}
                </p>
              </>
            ) : (
              t('job.bed_map_choose')
            )}
          </div>
          <Button
            wide
            variant="danger"
            disabled={!live || !selected || o.excluded.includes(selected.id)}
            onClick={onExclude}
          >
            {t('job.exclude_selected_object')}
          </Button>
          <p class="mt-2 text-xs text-muted">{t('job.this_protected_action_cannot_be')}</p>
        </div>
      </div>
    </div>
  )
  return <section aria-label={t('job.bed_map')}>{content}</section>
}
