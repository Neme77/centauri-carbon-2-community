import type { JSX } from 'preact'
import { cn } from '@/lib/utils'

// The numbered round mark (1-4) of the screws and of the steps: one size, one build, so every mark is an exact circle with the
// digit in the middle. A fixed width and height with aspect-square and shrink-0 keep it round in a flex row; leading-none and
// tabular-nums centre the digit. A button would otherwise be stretched to the global minimum height of the buttons
// (32 px on the desktop, 44 px on a touch screen) and become an ellipse, so `min-h-0!` and `text-xs!` stand against that; the
// touch target is the invisible frame around the circle (before:-inset-2.5 reaches 44 px).
const RING =
  'relative grid size-7 aspect-square shrink-0 place-items-center rounded-full border p-0 pt-px text-xs! leading-none font-medium tabular-nums'

type NumMarkProps = {
  n: number | string
  picked?: boolean
  class?: string
} & Omit<JSX.HTMLAttributes<HTMLElement>, 'class' | 'children' | 'size'>

export const NumMark = ({ n, picked = false, class: extra, onClick, ...rest }: NumMarkProps) =>
  onClick ? (
    <button
      type="button"
      class={cn(
        RING,
        'min-h-0! min-w-0 cursor-pointer before:absolute before:-inset-2.5 before:content-[""] focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-cyan',
        picked ? 'border-cyan bg-cyan text-ink' : 'border-muted',
        extra
      )}
      aria-pressed={picked}
      data-num-mark=""
      onClick={onClick}
      {...(rest as JSX.HTMLAttributes<HTMLButtonElement>)}
    >
      {n}
    </button>
  ) : (
    <span class={cn(RING, picked ? 'border-cyan bg-cyan text-ink' : 'border-muted', extra)} data-num-mark="" {...rest}>
      {n}
    </span>
  )
