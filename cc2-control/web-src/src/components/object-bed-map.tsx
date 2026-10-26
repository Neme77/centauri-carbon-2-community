import { X } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { t, tpl } from '@/lib/i18n'
import { objects } from '@/lib/objects'
import { BED_SIZE, onBed } from '@/lib/object-layout'
import { cn } from '@/lib/utils'

const ticks = [0, 50, 100, 150, 200, 250]

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
  const selected = o.entries.find(e => e.id === o.selected)
  const missing = o.entries.filter(e => !onBed(e.center)).length
  const content = (
    <div class={cn('cc2-object-map', onClose && 'cc2-object-map-expanded')}>
      {onClose && (
        <div class="mb-3 flex items-center justify-between gap-3 border-b border-edge pb-3">
          <DialogTitle>{t('job.bed_map')}</DialogTitle>
          <Button onClick={onClose} aria-label={t('job.bed_map_close')}>
            <X size={18} />
          </Button>
        </div>
      )}
      <p class="mb-3 text-xs text-muted">{t('job.bed_map_hint')}</p>
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
              <div class="cc2-object-map-surface" data-bed-map>
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
                </svg>
                {o.entries.map((e, i) => {
                  if (!onBed(e.center)) return null
                  const excluded = o.excluded.includes(e.id)
                  const current = e.id === o.current
                  const chosen = e.id === o.selected
                  return (
                    <button
                      type="button"
                      key={e.id}
                      data-bed-object-id={e.id}
                      data-current={current ? 'true' : undefined}
                      data-selected={chosen ? 'true' : undefined}
                      data-excluded={excluded ? 'true' : undefined}
                      aria-label={`${i + 1}. ${e.label} · X ${e.center[0].toFixed(1)} / Y ${e.center[1].toFixed(1)} mm${current ? ` · ${t('job.current_object')}` : ''}${excluded ? ` · ${t('job.excluded')}` : ''}`}
                      aria-pressed={chosen}
                      title={e.label}
                      disabled={excluded || !live}
                      class="cc2-object-map-marker focus-visible:outline-2 focus-visible:outline-offset-4 focus-visible:outline-cyan"
                      style={{
                        left: `${(e.center[0] / BED_SIZE) * 100}%`,
                        bottom: `${(e.center[1] / BED_SIZE) * 100}%`,
                      }}
                      onClick={() => objects.set({ selected: e.id })}
                    >
                      {i + 1}
                    </button>
                  )
                })}
              </div>
            </div>
            <p class="mt-2 text-center text-xs text-muted">{t('job.bed_map_front')}</p>
          </div>
          <div class="mt-3 flex flex-wrap justify-center gap-x-4 gap-y-2 text-xs">
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
    <Dialog onClose={onClose} width={1200} class="max-w-[calc(100vw-2rem)]" label={t('job.bed_map')}>
      {content}
    </Dialog>
  ) : (
    <section aria-label={t('job.bed_map')}>{content}</section>
  )
}
