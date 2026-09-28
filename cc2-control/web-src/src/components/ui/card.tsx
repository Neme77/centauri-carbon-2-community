import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'
import { Icon } from '@/components/icons'
import { t } from '@/lib/i18n'

export const Card = ({ class: c, children, ...p }: { class?: string; children?: ComponentChildren } & preact.JSX.HTMLAttributes<HTMLElement>) => (
  <section class={cn('min-w-0 rounded-lg border border-edge bg-panel p-3.5', c)} {...p}>{children}</section>
)

export const CardHead = ({ icon, title, end, sub }: { icon?: string; title: string; end?: ComponentChildren; sub?: string }) => (
  <div class="mb-3 flex min-h-7 items-center gap-2.5 border-b border-edge pb-2.5">
    {icon && <Icon n={icon} class="text-cyan" />}
    <div class="min-w-0"><h2 class="text-base font-semibold">{t(title)}</h2>{sub && <small class="text-muted">{t(sub)}</small>}</div>
    {end !== undefined && <span class="ml-auto text-xs text-muted">{end}</span>}
  </div>
)

export const Page = ({ title, sub, tags, children }: { title: string; sub?: string; tags?: ComponentChildren; children: ComponentChildren }) => (
  <>
    <div class="mb-4 flex items-center justify-between gap-3">
      <div><h2 class="text-2xl font-semibold">{t(title)}</h2>{sub && <p class="text-muted">{t(sub)}</p>}</div>
      <div class="hidden gap-2 md:flex">{tags}</div>
    </div>
    {children}
  </>
)
