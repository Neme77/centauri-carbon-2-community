import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'

const tone = {
  default: 'border-edge text-muted',
  warning: 'border-amber text-amber bg-amber/10',
  ok: 'border-green text-green bg-green/10',
}
export const Tag = ({
  tone: k = 'default',
  class: c,
  children,
}: {
  tone?: keyof typeof tone
  class?: string
  children: ComponentChildren
}) => (
  <span class={cn('inline-flex items-center gap-1.5 rounded-full border px-2.5 py-0.5 text-[11px]', tone[k], c)}>
    {children}
  </span>
)

export const Pip = ({ hollow }: { hollow?: boolean }) => (
  <i
    class={cn(
      'inline-block size-2 shrink-0 rounded-full align-middle',
      hollow ? 'border border-current' : 'bg-current'
    )}
  />
)

const dots = { red: 'bg-red', blue: 'bg-blue', amber: 'bg-amber', green: 'bg-green' }
export const Dot = ({ c = 'green' }: { c?: keyof typeof dots }) => (
  <i class={cn('inline-block size-2.5 rounded-full', dots[c])} />
)
