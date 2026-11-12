import { ChevronLeft, ChevronRight, X } from 'lucide-preact'
import { useEffect, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { t, tpl } from '@/lib/i18n'
import {
  cachedLayerPath,
  fetchLayerPath,
  splitLayerPaths,
  type LayerPath,
  type LayerPathPolyline,
} from '@/lib/layer-path'
import { objects } from '@/lib/objects'
import { BED_SIZE, onBed } from '@/lib/object-layout'
import { printer, view } from '@/lib/state'
import { cn } from '@/lib/utils'

const ticks = [0, 50, 100, 150, 200, 250]
const STORAGE = 'internal'
// Rapid stepping or a fast Live layer change requests only the layer it settles on.
const LAYER_SETTLE_MS = 350
// A busy scan of a large file takes up to the backend's 20 s deadline.
const LAYER_BUSY_RETRY_MS = 1500
const LAYER_BUSY_RETRIES = 15
const layerPathBusy = (error: unknown) => (error as { status?: number } | null)?.status === 503
/* The backend gave up on the scan (time or size budget): say so instead of "not available". */
const layerPathLimit = (error: unknown) => {
  const e = error as { status?: number; data?: { error?: unknown } } | null
  return e?.status === 413 && e.data?.error === 'Layer path scan limit reached'
}

const pointsAttr = (path: LayerPathPolyline) => path.map(([x, y]) => `${x},${BED_SIZE - y}`).join(' ')

function markerPoint(
  center: [number, number] | null,
  polygon: [number, number][] | null,
  edge: boolean
): [number, number] | null {
  if (!center) return null
  if (!edge) return center
  if (!polygon?.length) {
    return [Math.min(BED_SIZE, center[0] + 14), Math.min(BED_SIZE, center[1] + 14)]
  }
  let best = polygon[0]
  let score = Number.NEGATIVE_INFINITY
  for (const p of polygon) {
    const s = p[0] - center[0] + (p[1] - center[1])
    if (s > score) {
      score = s
      best = p
    }
  }
  return best
}

const formatZ = (z: number) => (Math.round(z * 100) / 100).toFixed(2)

function LayerPolylines({ paths, kind }: { paths: LayerPathPolyline[]; kind: 'prev' | 'printed' | 'remain' }) {
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
              kind === 'printed' && 'cc2-layer-path-printed',
              kind === 'remain' && 'cc2-layer-path-remain'
            )}
          />
        ) : null
      )}
    </>
  )
}

// Map prototype: Nick Plateringen-Paul. Single-view refinement: DamiBFryta.
// Both sizes read the same model; selection never changes the original G-code ID.
export const ObjectBedMap = ({
  onClose,
  onExclude,
  live,
}: {
  onClose?: () => void
  onExclude: () => void
  live: boolean
}) => {
  const o = objects.use()
  const printerState = printer.use()
  const job = view(printerState.data)
  const selected = o.entries.find(e => e.id === o.selected)
  const missing = o.entries.filter(e => !onBed(e.center)).length
  const printerLayer = Number(job.layer) || 0
  const printerTotal = typeof job.total === 'number' ? job.total : 0
  const file = job.rawFilename
  const [tab, setTab] = useState<'objects' | 'layer'>('objects')
  const [liveFollow, setLiveFollow] = useState(true)
  const [manualLayer, setManualLayer] = useState(1)
  const [path, setPath] = useState<LayerPath | null>(null)
  const [prevPath, setPrevPath] = useState<LayerPath | null>(null)
  const [loading, setLoading] = useState(false)
  const [unavailable, setUnavailable] = useState<false | 'limit' | 'missing'>(false)
  const viewLayer = liveFollow ? printerLayer : manualLayer
  const totalLayers = path?.total_layers || printerTotal
  const expanded = Boolean(onClose)
  const layerTab = expanded && tab === 'layer'
  const nozzleRaw = printerState.data?.motion
  const nozzle: [number, number] | null =
    typeof nozzleRaw?.x === 'number' && typeof nozzleRaw?.y === 'number' ? [nozzleRaw.x, nozzleRaw.y] : null
  const nozzleOnBed = onBed(nozzle)

  // The backend scans one uncached layer at a time and keeps scanning after a
  // request is abandoned. So the current layer is requested alone, after rapid
  // layer changes have settled, and a busy reply is retried for the latest layer
  // only. The previous layer comes from the client cache, or is fetched after
  // the current layer has been drawn; it never decides whether the current one
  // is shown. The effect depends on a stable flag, not on the onClose callback,
  // so a parent render does not restart a pending request.
  useEffect(() => {
    if (!expanded || tab !== 'layer') return
    if (!file || viewLayer < 1) {
      // While the print prepares (layer 0), Live waits for layer 1 instead of failing.
      setPath(null)
      setPrevPath(null)
      setLoading(false)
      setUnavailable(file && liveFollow ? false : 'missing')
      return
    }
    const ac = new AbortController()
    let timer: ReturnType<typeof setTimeout> | undefined
    const wait = (ms: number) => new Promise<void>(resolve => (timer = setTimeout(resolve, ms)))
    const hit = cachedLayerPath(STORAGE, file, viewLayer)
    const prevHit = viewLayer > 1 ? cachedLayerPath(STORAGE, file, viewLayer - 1) : undefined
    setPath(hit || null)
    setPrevPath(prevHit || null)
    setLoading(!hit)
    setUnavailable(false)
    const load = async () => {
      try {
        if (!hit) {
          await wait(LAYER_SETTLE_MS)
          let current: LayerPath | null = null
          for (let attempt = 0; !current; attempt++) {
            if (ac.signal.aborted) return
            try {
              current = await fetchLayerPath(STORAGE, file, viewLayer, ac.signal)
            } catch (error) {
              if (ac.signal.aborted) return
              if (!layerPathBusy(error) || attempt >= LAYER_BUSY_RETRIES) throw error
              await wait(LAYER_BUSY_RETRY_MS)
            }
          }
          if (ac.signal.aborted) return
          setPath(current)
          setLoading(false)
        }
        if (viewLayer > 1 && !prevHit) {
          const previous = await fetchLayerPath(STORAGE, file, viewLayer - 1, ac.signal).catch(() => null)
          if (!ac.signal.aborted) setPrevPath(previous)
        }
      } catch (error) {
        if (ac.signal.aborted) return
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
  }, [expanded, tab, file, viewLayer, liveFollow])

  const openLayer = () => setTab('layer')
  const followLive = () => {
    openLayer()
    setLiveFollow(true)
  }
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
  const { printed, remaining } = splitLayerPaths(path?.paths || [], nozzleOnBed ? nozzle : null, splitMode)
  const zValue = path?.z ?? (liveFollow && typeof nozzleRaw?.z === 'number' ? nozzleRaw.z : null)
  const waitingFirstLayer = layerTab && liveFollow && Boolean(file) && printerLayer < 1
  // The Objects tab shows every layer's outline, so it names only the layer being printed.
  const status = !onClose
    ? ''
    : !layerTab
      ? printerLayer >= 1 && printerTotal > 0
        ? tpl('job.bed_map_printing_layer', { n: printerLayer, total: printerTotal })
        : ''
      : waitingFirstLayer
        ? t('job.bed_map_waiting_first_layer')
        : layerTab && loading
          ? t('job.bed_map_loading_layer')
          : layerTab && unavailable
            ? t(unavailable === 'limit' ? 'job.bed_map_layer_limit' : 'job.bed_map_layer_unavailable')
            : viewLayer >= 1 && totalLayers > 0 && zValue != null
              ? tpl('job.bed_map_layer_status', { n: viewLayer, total: totalLayers, z: formatZ(zValue) })
              : viewLayer >= 1 && totalLayers > 0
                ? tpl('job.bed_map_layer_status_no_z', { n: viewLayer, total: totalLayers })
                : layerTab
                  ? t('job.bed_map_layer_unavailable')
                  : ''

  const content = (
    <div
      class={cn('cc2-object-map', onClose && 'cc2-object-map-expanded')}
      data-bed-map-tab={onClose ? tab : undefined}
    >
      {onClose && (
        <div class="mb-3 flex items-center justify-between gap-3 border-b border-edge pb-3">
          <DialogTitle>{t('job.bed_map')}</DialogTitle>
          <Button onClick={onClose} aria-label={t('job.bed_map_close')}>
            <X size={18} />
          </Button>
        </div>
      )}
      <p class="mb-3 text-xs text-muted">{t('job.bed_map_hint')}</p>
      {onClose && (
        <div class="mb-3 flex flex-wrap items-center gap-2">
          <fieldset class="m-0 flex h-11 min-w-0 overflow-hidden rounded-md border border-edge p-0">
            <legend class="sr-only">{t('job.bed_map_view')}</legend>
            <Button
              class="h-11 min-h-11 rounded-none border-0 px-3.5 whitespace-nowrap"
              variant={tab === 'objects' ? 'primary' : 'default'}
              aria-pressed={tab === 'objects'}
              onClick={() => setTab('objects')}
            >
              {t('job.bed_map_objects')}
            </Button>
            <Button
              class="h-11 min-h-11 rounded-none border-0 border-l border-edge px-3.5 whitespace-nowrap"
              variant={tab === 'layer' ? 'primary' : 'default'}
              aria-pressed={tab === 'layer'}
              onClick={openLayer}
            >
              {t('job.bed_map_layer_path')}
            </Button>
          </fieldset>
          <div class="flex items-center gap-2">
            <Button
              class="h-11 w-11 min-h-11 p-0"
              aria-label={t('job.bed_map_previous_layer')}
              disabled={viewLayer <= 1 || !file}
              onClick={() => stepLayer(-1)}
            >
              <ChevronLeft size={18} />
            </Button>
            <Button
              class="h-11 w-11 min-h-11 p-0"
              aria-label={t('job.bed_map_next_layer')}
              disabled={!file || (totalLayers > 0 && viewLayer >= totalLayers)}
              onClick={() => stepLayer(1)}
            >
              <ChevronRight size={18} />
            </Button>
            <Button
              class="h-11 min-h-11 px-3.5 whitespace-nowrap"
              variant={liveFollow ? 'primary' : 'default'}
              aria-pressed={liveFollow}
              data-layer-live
              disabled={!file}
              onClick={followLive}
            >
              {t('job.bed_map_live')}
            </Button>
          </div>
          <p class="min-h-4 min-w-0 flex-1 text-xs text-muted whitespace-nowrap" data-layer-status role="status">
            {status}
          </p>
        </div>
      )}
      <div class="cc2-object-map-layout grid min-w-0 gap-3.5 cc2-md:grid-cols-2">
        <div class="min-w-0">
          <div class="cc2-object-map-frame">
            <div class="mb-2 flex justify-between text-xs text-muted">
              <span>
                {BED_SIZE} × {BED_SIZE} mm
              </span>
              <span>Y ↑ · X →</span>
            </div>
            <div class="cc2-object-map-plate">
              <div class="cc2-object-map-surface" data-bed-map aria-busy={layerTab && loading ? 'true' : undefined}>
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
                        <polygon
                          key={e.id}
                          data-bed-outline-id={e.id}
                          data-selected={e.id === o.selected ? 'true' : undefined}
                          data-current={e.id === o.current ? 'true' : undefined}
                          data-excluded={o.excluded.includes(e.id) ? 'true' : undefined}
                          points={e.polygon.map(([x, y]) => `${x},${BED_SIZE - y}`).join(' ')}
                          class="cc2-object-map-outline"
                        />
                      )
                  )}
                  {layerTab && !unavailable && prevPath && <LayerPolylines paths={prevPath.paths} kind="prev" />}
                  {layerTab && !unavailable && path && (
                    <>
                      <LayerPolylines paths={remaining} kind="remain" />
                      <LayerPolylines paths={printed} kind="printed" />
                    </>
                  )}
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
                      onClick={() => objects.set({ selected: e.id })}
                    >
                      {i + 1}
                    </button>
                  )
                })}
                {layerTab && !unavailable && nozzleOnBed && nozzle && (
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
                {layerTab && loading && (
                  <div class="cc2-layer-path-status" role="status">
                    {t('job.bed_map_loading_layer')}
                  </div>
                )}
              </div>
            </div>
            <p class="mt-2 text-center text-xs text-muted">{t('job.bed_map_front')}</p>
          </div>
          <div class="cc2-object-map-legend mt-2 flex flex-wrap justify-center gap-x-4 gap-y-2 text-xs">
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
            {onClose && (
              <span class="flex items-center gap-1.5">
                <i class="cc2-layer-path-swatch" />
                {t('job.bed_map_layer_legend')}
              </span>
            )}
          </div>
          {missing > 0 && <p class="mt-3 text-xs text-muted">{tpl('job.bed_map_missing', { n: missing })}</p>}
        </div>
        <div class="flex min-w-0 flex-col">
          <p class="mb-2 text-xs text-muted">{t('job.object_position_order')}</p>
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
                    onClick={() => objects.set({ selected: e.id })}
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
          <div
            class="my-3 rounded-md border border-edge bg-field p-3 text-xs [overflow-wrap:anywhere]"
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
  return onClose ? (
    <Dialog onClose={onClose} width={1200} label={t('job.bed_map')} sheet>
      {content}
    </Dialog>
  ) : (
    <section aria-label={t('job.bed_map')}>{content}</section>
  )
}
