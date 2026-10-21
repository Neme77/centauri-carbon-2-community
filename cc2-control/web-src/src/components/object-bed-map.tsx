import { X } from 'lucide-preact'
import { Button } from '@/components/ui/button'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { t, tpl } from '@/lib/i18n'
import { objects } from '@/lib/objects'
import { BED_SIZE, onBed } from '@/lib/object-layout'
import { cn } from '@/lib/utils'

const ticks = [0, 50, 100, 150, 200, 250]

// Inspired by Nick Plateringen-Paul's bed-map prototype. Use the object model,
// never parsed button text, CSS classes, disabled state or another polling timer.
export const ObjectBedMap = ({
  onClose,
  onExclude,
  live,
}: {
  onClose: () => void
  onExclude: () => void
  live: boolean
}) => {
  const o = objects.use()
  const selected = o.entries.find(e => e.id === o.selected)
  const missing = o.entries.filter(e => !onBed(e.center)).length
  return (
    <Dialog onClose={onClose} width={880} class="max-w-[calc(100vw-2rem)]" label={t('job.bed_map')}>
      <div class="mb-3 flex items-center justify-between gap-3 border-b border-edge pb-3">
        <DialogTitle>{t('job.bed_map')}</DialogTitle>
        <Button onClick={onClose} aria-label={t('job.bed_map_close')}>
          <X size={18} />
        </Button>
      </div>
      <p class="mb-4 text-sm text-muted">{t('job.bed_map_hint')}</p>
      <div class="grid min-w-0 gap-5 cc2-md:grid-cols-2">
        <div class="min-w-0">
          <div class="mb-2 flex justify-between text-xs text-muted">
            <span>
              {BED_SIZE} × {BED_SIZE} mm
            </span>
            <span>Y ↑ · X →</span>
          </div>
          <div class="p-4">
            <div class="relative aspect-square border-2 border-edge bg-field" data-bed-map>
              <svg viewBox={`0 0 ${BED_SIZE} ${BED_SIZE}`} class="absolute inset-0 h-full w-full" aria-hidden="true">
                {ticks.map(n => (
                  <path
                    key={n}
                    d={`M ${n} 0 V ${BED_SIZE} M 0 ${BED_SIZE - n} H ${BED_SIZE}`}
                    fill="none"
                    stroke="var(--edge)"
                    stroke-width="0.5"
                  />
                ))}
                {o.entries.map(
                  e =>
                    e.polygon &&
                    onBed(e.center) && (
                      <polygon
                        key={e.id}
                        points={e.polygon.map(([x, y]) => `${x},${BED_SIZE - y}`).join(' ')}
                        fill={
                          o.excluded.includes(e.id)
                            ? 'var(--red)'
                            : e.id === o.selected
                              ? 'var(--cyan)'
                              : e.id === o.current
                                ? 'var(--amber)'
                                : 'var(--muted)'
                        }
                        fill-opacity="0.14"
                        stroke={
                          o.excluded.includes(e.id)
                            ? 'var(--red)'
                            : e.id === o.selected
                              ? 'var(--cyan)'
                              : e.id === o.current
                                ? 'var(--amber)'
                                : 'var(--muted)'
                        }
                        stroke-width="1"
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
                    aria-label={`${i + 1}. ${e.label} · X ${e.center[0].toFixed(1)} / Y ${e.center[1].toFixed(1)} mm${current ? ` · ${t('job.current_object')}` : ''}${excluded ? ` · ${t('job.excluded')}` : ''}`}
                    aria-pressed={chosen}
                    title={e.label}
                    disabled={excluded || !live}
                    class={cn(
                      'absolute z-10 grid h-8 w-8 -translate-x-1/2 translate-y-1/2 place-items-center rounded-md border-2 text-xs font-bold shadow-sm focus-visible:outline-2 focus-visible:outline-offset-4 focus-visible:outline-cyan disabled:cursor-not-allowed',
                      excluded
                        ? 'border-red bg-panel text-red line-through'
                        : chosen
                          ? 'border-cyan bg-cyan text-ink cursor-pointer'
                          : 'border-muted bg-panel text-fg cursor-pointer hover:border-cyan',
                      current && !excluded && 'ring-2 ring-amber ring-offset-2 ring-offset-field'
                    )}
                    style={{ left: `${(e.center[0] / BED_SIZE) * 100}%`, bottom: `${(e.center[1] / BED_SIZE) * 100}%` }}
                    onClick={() => objects.set({ selected: e.id })}
                  >
                    {i + 1}
                  </button>
                )
              })}
            </div>
          </div>
          <p class="text-center text-xs text-muted">{t('job.bed_map_front')}</p>
          <div class="mt-4 flex flex-wrap justify-center gap-x-4 gap-y-2 text-xs">
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
          {missing > 0 && <p class="mt-3 text-sm text-muted">{tpl('job.bed_map_missing', { n: missing })}</p>}
        </div>
        <div class="flex min-w-0 flex-col">
          <div class="grid max-h-64 gap-2 overflow-y-auto p-1">
            {o.entries.map((e, i) => {
              const excluded = o.excluded.includes(e.id)
              const current = e.id === o.current
              return (
                <Button
                  key={e.id}
                  data-bed-list-id={e.id}
                  aria-pressed={e.id === o.selected}
                  variant={e.id === o.selected ? 'active' : 'default'}
                  disabled={excluded || !live}
                  class={cn(
                    'h-auto justify-start whitespace-normal text-left text-xs [overflow-wrap:anywhere]',
                    excluded && 'text-red'
                  )}
                  onClick={() => objects.set({ selected: e.id })}
                >
                  <span class="shrink-0 font-bold">{i + 1}.</span>
                  <span>
                    {e.label}
                    {excluded ? ` · ${t('job.excluded')}` : ''}
                  </span>
                  {current && (
                    <span role="img" class="ml-auto shrink-0 text-amber" aria-label={t('job.current_object')}>
                      ●
                    </span>
                  )}
                </Button>
              )
            })}
          </div>
          <p class="my-4 min-w-0 text-sm [overflow-wrap:anywhere]">
            {selected ? (
              <>
                <span class="text-muted">{t('job.bed_map_selected')}: </span>
                <b>{selected.label}</b>
              </>
            ) : (
              t('job.bed_map_choose')
            )}
          </p>
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
    </Dialog>
  )
}
