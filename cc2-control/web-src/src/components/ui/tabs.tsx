import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'

export type TabItem = { id: string; label: string; icon: ComponentChildren }

// Page-level tab bar shared by Bed Levelling and Settings. One row always: icons drop below sm, then the bar scrolls sideways
export const Tabs = ({
  items,
  value,
  onChange,
}: {
  items: TabItem[]
  value: string
  onChange: (id: string) => void
}) => (
  <div role="tablist" class="mb-3.5 flex gap-1.5 overflow-x-auto border-b border-edge [scrollbar-width:none]">
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
            'flex shrink-0 items-center gap-2 whitespace-nowrap rounded-t-lg border border-b-[3px] border-edge px-2 py-2.5 text-sm sm:gap-3 sm:px-6 sm:py-3 sm:text-base',
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
