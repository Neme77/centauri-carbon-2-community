import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'

export type TabItem = { id: string; label: string; icon: ComponentChildren }

// Page-level tab bar shared by Bed Levelling and Settings.
export const Tabs = ({
  items,
  value,
  onChange,
}: {
  items: TabItem[]
  value: string
  onChange: (id: string) => void
}) => (
  <div role="tablist" class="mb-3.5 flex flex-wrap gap-1.5 border-b border-edge">
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
            'flex items-center gap-2 rounded-t-lg border border-b-[3px] border-edge px-3 py-2.5 text-sm sm:gap-3 sm:px-6 sm:py-3 sm:text-base',
            on ? 'border-b-cyan bg-field text-cyan' : 'border-b-transparent text-muted'
          )}
        >
          {item.icon}
          {item.label}
        </button>
      )
    })}
  </div>
)
