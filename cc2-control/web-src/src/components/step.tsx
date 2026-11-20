import type { ComponentChildren } from 'preact'
import { ChevronDown } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { levelOpen } from '@/lib/level-state'
import { NumMark } from '@/components/num-mark'

// One numbered step of the Bed alignment tab. The head always shows the step's state, folded or not.
export const Step = ({
  n,
  title,
  summary,
  aside,
  defaultOpen = true,
  children,
}: {
  n: number
  title: string
  summary?: string
  // Short text in the head, next to the title (under it on a phone); shown while the step is open.
  aside?: ComponentChildren
  defaultOpen?: boolean
  children: ComponentChildren
}) => {
  const state = levelOpen.use()
  const open = state[`step${n}`] ?? defaultOpen
  const body = `level-step-${n}`
  return (
    <section class="mt-3.5 rounded-lg border border-edge" data-level-step={n} data-open={open ? 'yes' : 'no'}>
      <button
        type="button"
        class="flex min-h-12 w-full flex-wrap items-center gap-x-3 gap-y-1 rounded-lg px-3 py-2 text-left"
        aria-expanded={open}
        aria-controls={body}
        onClick={() => levelOpen.set({ [`step${n}`]: !open })}
      >
        <NumMark n={n} class="order-1" />
        <span class="order-2 min-w-0 flex-1">
          <strong class="block text-base font-semibold">{title}</strong>
          {summary && (
            <small class="block text-xs font-normal break-words text-muted" data-level-summary={n}>
              {summary}
            </small>
          )}
        </span>
        {aside && open && (
          <span class="order-5 basis-full pl-[2.375rem] text-xs font-normal text-muted" data-level-aside={n}>
            {aside}
          </span>
        )}
        <ChevronDown
          size={18}
          strokeWidth={1.75}
          class={cn('order-3 shrink-0 text-muted transition-transform', open && 'rotate-180')}
        />
      </button>
      <div id={body} class={cn('px-3 pb-3', !open && 'hidden')}>
        {children}
      </div>
    </section>
  )
}
