import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { printer } from '@/lib/state'
import { t, tpl } from '@/lib/i18n'
import { notify } from '@/lib/api'

// Shown on every page once a chamber preheat holds its target, until dismissed or the session ends.
export const ChamberReady = () => {
  const { data, ok } = printer.use()
  const session = data?.bed_drying
  const ready = Boolean(ok && session?.active && session.mode === 'preheat' && session.state === 'holding')
  const [dismissed, setDismissed] = useState(false)
  const announced = useRef(false)
  const title = session?.levelled ? 'preheat.levelled_title' : 'preheat.ready_title'
  useEffect(() => {
    if (ready && !announced.current) notify(t(title))
    announced.current = ready
    if (!ready) setDismissed(false)
  }, [ready])
  if (!ready || dismissed) return null
  return (
    <Card
      role="region"
      aria-label={t(title)}
      data-chamber-ready
      class="fixed right-3 bottom-3 z-40 w-[min(420px,calc(100vw-24px))] shadow-2xl"
    >
      <CardHead icon="temp" title={title} />
      <p class="text-muted">
        {tpl(session.levelled ? 'preheat.levelled_detail' : 'preheat.ready_detail', {
          chamber: Number(session.chamber).toFixed(1),
          target: session.chamber_target,
        })}
      </p>
      <div class="mt-3 flex flex-wrap gap-2">
        <Button onClick={() => setDismissed(true)}>{t('completed.acknowledge')}</Button>
      </div>
    </Card>
  )
}
