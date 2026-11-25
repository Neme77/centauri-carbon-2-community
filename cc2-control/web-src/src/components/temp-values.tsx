import type { ComponentChildren } from 'preact'
import { printer } from '@/lib/state'
import { i18n, type Key, t } from '@/lib/i18n'
import { cn } from '@/lib/utils'
import { usePoll } from '@/lib/poll'
import { store } from '@/lib/store'
import { formatLiveValue, formatTargetValue, liveStale } from '@/lib/temp-values'

// One row per heater or sensor, in fixed columns: name, live value (with its mark), target (the bed only, else a dash),
// value measured at. Each column has its own label, the numbers stand right-aligned in tabular figures with a fixed
// width, so a changing live value moves nothing. On a narrow screen the values stand one under the other.
export type TempRow = {
  id: 'bed' | 'nozzle' | 'chamber'
  label: Key
  target?: { value: number; approx: boolean } | null
  // The actual temperature at the last measurement; absent until something was measured.
  measured?: number | null
  live?: { value: number | null; digits: 0 | 1 }
}

// The age of the live values is measured on the clock printer.at uses (performance.now()), advanced once a second.
const clock = store({ now: performance.now() })
const advance = () => clock.set({ now: performance.now() })

// A labelled number: the label left, the number right-aligned in a fixed width.
const Cell = ({
  label,
  children,
  class: extra,
  ...rest
}: {
  label: string
  children: ComponentChildren
  class?: string
  'data-value': string
}) => (
  <dd class={cn('flex flex-wrap items-baseline gap-x-1.5', extra)} {...rest}>
    <span class="text-muted">{label}</span> <span class="tabular-nums">{children}</span>
  </dd>
)

export const TempValues = ({ rows }: { rows: TempRow[] }) => {
  const { at, ok } = printer.use()
  usePoll(advance, 1000)
  const { now } = clock.use()
  const lang = i18n.get().lang
  const stale = liveStale(at, ok, now)
  return (
    <dl class="grid gap-1 text-xs" data-temp-values>
      {rows.map(r => (
        <div
          key={r.id}
          class="grid grid-cols-[4.5rem_minmax(0,1fr)] items-baseline gap-x-2 gap-y-0.5 cc2-sm:grid-cols-[4.5rem_9rem_7rem_minmax(0,1fr)]"
          data-temp-row={r.id}
        >
          <dt class="font-medium">{t(r.label)}</dt>
          {r.live ? (
            <dd
              data-value="live"
              data-stale={stale ? 'true' : 'false'}
              class={cn(
                'col-start-2 flex flex-wrap items-baseline gap-x-1.5 cc2-sm:col-start-auto',
                stale && 'text-muted'
              )}
            >
              <span class="min-w-[3.75rem] text-right tabular-nums">
                {r.live.value == null ? '–' : `${formatLiveValue(r.live.value, lang, r.live.digits)} °C`}
              </span>{' '}
              <span class={cn('rounded px-1 text-[0.7rem]', stale ? 'border border-edge' : 'bg-cyan/15 text-cyan')}>
                {stale ? t('bed.value_stale') : t('common.live')}
              </span>
            </dd>
          ) : (
            <dd />
          )}
          {r.target ? (
            <Cell label={t('bed.value_target')} data-value="target" class="col-start-2 cc2-sm:col-start-auto">
              {formatTargetValue(r.target.value, r.target.approx)} °C
            </Cell>
          ) : (
            <Cell label={t('bed.value_target')} data-value="target-none" class="col-start-2 cc2-sm:col-start-auto">
              –
            </Cell>
          )}
          {r.measured != null ? (
            <Cell label={t('bed.value_measured_at')} data-value="measured" class="col-start-2 cc2-sm:col-start-auto">
              {formatLiveValue(r.measured, lang, r.id === 'chamber' ? 0 : 1)} °C
            </Cell>
          ) : null}
        </div>
      ))}
    </dl>
  )
}
