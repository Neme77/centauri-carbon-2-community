import { cn } from '@/lib/utils'

// Full width unless the caller sets its own width (clsx does not resolve conflicting utilities).
const wide = (c?: string) => (/(^|\s)w-/.test(c || '') ? c : cn('w-full', c))
const base = 'min-h-9 min-w-0 rounded-md border border-edge bg-field px-2.5 py-1.5 text-fg disabled:opacity-60'
export const Input = ({ class: c, ...p }: preact.JSX.InputHTMLAttributes<HTMLInputElement>) => (
  <input class={cn(base, wide(c as string))} {...p} />
)
export const Select = ({ class: c, ...p }: preact.JSX.SelectHTMLAttributes<HTMLSelectElement>) => (
  <select class={cn(base, wide(c as string))} {...p} />
)
export const Switch = ({ on, onClick, label }: { on: boolean; onClick?: () => void; label: string }) => (
  <button
    type="button"
    role="switch"
    aria-checked={on}
    aria-label={label}
    onClick={onClick}
    class={cn('flex h-5.5 w-10 shrink-0 rounded-full p-0.75', on ? 'bg-cyan' : 'bg-edge')}
  >
    <i class={cn('size-4 rounded-full bg-white transition-all', on && 'ml-auto')} />
  </button>
)
