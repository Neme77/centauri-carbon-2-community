import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'
import { Icon } from '@/components/icons'
import { type Key, t, tpl } from '@/lib/i18n'

export const Card = ({
  class: c,
  children,
  ...p
}: { class?: string; children?: ComponentChildren } & preact.JSX.HTMLAttributes<HTMLElement>) => (
  <section class={cn('min-w-0 rounded-lg border border-edge bg-panel p-3.5 [corner-shape:bevel]', c)} {...p}>
    {children}
  </section>
)

export const CardHead = ({
  icon,
  title,
  end,
  sub,
  values,
}: {
  icon?: string
  title: Key
  end?: ComponentChildren
  sub?: Key
  values?: Record<string, string | number>
}) => (
  <div class="mb-3 flex min-h-7 items-center gap-2.5 border-b border-edge pb-2.5">
    {icon && <Icon n={icon} class="text-cyan" />}
    <div class="min-w-0">
      <h2 class="text-base font-semibold">{values ? tpl(title, values) : t(title)}</h2>
      {sub && <small class="text-muted">{t(sub)}</small>}
    </div>
    {end !== undefined && <span class="ml-auto text-xs text-muted">{end}</span>}
  </div>
)

export const Page = ({
  title,
  sub,
  tags,
  children,
}: {
  title: Key
  sub?: Key
  tags?: ComponentChildren
  children: ComponentChildren
}) => (
  <>
    <div class="mb-4 flex min-w-0 flex-col gap-3 cc2-sm:flex-row cc2-sm:items-start cc2-sm:justify-between">
      <div class="min-w-0">
        <h2 class="text-2xl font-semibold [overflow-wrap:anywhere]">{t(title)}</h2>
        {sub && <p class="text-muted [overflow-wrap:anywhere]">{t(sub)}</p>}
      </div>
      {tags !== undefined && <div class="flex min-w-0 flex-wrap items-center gap-2 cc2-sm:justify-end">{tags}</div>}
    </div>
    {children}
  </>
)
