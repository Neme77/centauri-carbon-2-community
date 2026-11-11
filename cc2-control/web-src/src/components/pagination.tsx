import { Button } from '@/components/ui/button'
import { t, tpl } from '@/lib/i18n'

export const PAGE_SIZE = 15
export const pageIndex = (page: number, total: number) => Math.max(0, Math.min(page, Math.ceil(total / PAGE_SIZE) - 1))
export const Pagination = ({
  page,
  total,
  onChange,
}: {
  page: number
  total: number
  onChange: (page: number) => void
}) => {
  const current = pageIndex(page, total)
  const pages = Math.max(1, Math.ceil(total / PAGE_SIZE))
  if (!total) return null
  return (
    <nav
      aria-label={t('common.pagination')}
      class="cc2-pagination mt-3 flex flex-wrap items-center justify-between gap-2 border-t border-edge pt-3"
    >
      <Button disabled={current === 0} onClick={() => onChange(current - 1)}>
        {t('common.previous_page')}
      </Button>
      <span class="text-center text-xs text-muted" aria-live="polite">
        {tpl('common.page_of_total', { page: current + 1, pages, total })}
      </span>
      <Button disabled={current + 1 >= pages} onClick={() => onChange(current + 1)}>
        {t('common.next_page')}
      </Button>
    </nav>
  )
}
