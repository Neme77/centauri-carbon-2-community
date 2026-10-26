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
      'mb-3.5 flex gap-1.5 border-b border-edge',
      wrap ? 'flex-wrap' : 'overflow-x-auto [scrollbar-width:none]'
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
            'flex shrink-0 items-center gap-2 whitespace-nowrap rounded-t-lg border border-b-[3px] border-edge px-2 py-2.5 text-sm sm:py-3 sm:text-base',
            wrap ? 'sm:gap-2 sm:px-3' : 'sm:gap-3 sm:px-6',
            on ? 'border-b-cyan bg-field text-cyan' : 'border-b-transparent text-muted'
          )}
        >
          <span class="hidden sm:contents">{item.icon}</span>
          {item.label}
        </button>
      )
    })}
  </div>
)
