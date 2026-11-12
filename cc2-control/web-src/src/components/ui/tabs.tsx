import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'

export type TabItem = { id: string; label: string; icon: ComponentChildren }

// Bed tabs keep their scrolling row. Settings can wrap so every section stays visible.
export const Tabs = ({
  items,
  value,
  onChange,
  wrap = false,
}: {
  items: TabItem[]
  value: string
  onChange: (id: string) => void
  wrap?: boolean
}) => (
  <div
    role="tablist"
    class={cn(
      'mb-3.5 flex gap-1.5',
      wrap
        ? 'flex-wrap border-b border-edge'
        : 'overflow-x-auto shadow-[inset_0_-1px_0_var(--edge)] [scrollbar-width:none]'
    )}
  >
    {items.map(item => {
      const on = item.id === value
      return (
        <button
          type="button"
          role="tab"
          aria-selected={on}
          key={item.id}
          onClick={() => onChange(item.id)}
          class={cn(
            'flex shrink-0 items-center gap-2 whitespace-nowrap font-medium transition-colors',
            wrap
              ? 'rounded-t-lg border border-b-[3px] border-edge px-2 py-2.5 text-sm sm:gap-2 sm:px-3 sm:py-3 sm:text-base'
              : 'border-b-2 px-3 py-2.5 text-sm sm:gap-2.5 sm:px-4',
            wrap
              ? on
                ? 'border-b-cyan bg-field text-cyan'
                : 'border-b-transparent text-muted'
              : on
                ? 'border-cyan text-cyan'
                : 'border-transparent text-muted hover:text-fg'
          )}
        >
          <span class="hidden sm:contents">{item.icon}</span>
          {item.label}
        </button>
      )
    })}
  </div>
)
