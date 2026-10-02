import type { ComponentChildren } from 'preact'
import { useEffect, useRef, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { Dot } from '@/components/ui/badge'
import { Card, CardHead } from '@/components/ui/card'
import { Icon } from '@/components/icons'
import { type Key, t } from '@/lib/i18n'
import { camera } from '@/lib/state'
import { Button } from '@/components/ui/button'

export const Reading = ({
  dot,
  label,
  value,
  target,
}: {
  dot?: 'red' | 'blue' | 'amber'
  label: Key
  value: string
  target?: string
}) => (
  <div class="flex items-center gap-2 text-xs">
    {dot && <Dot c={dot} />}
    {t(label)}
    <b class="ml-auto text-[13px] font-semibold">{value}</b>
    {target && <span class="min-w-6 text-right text-xs text-muted">{target}</span>}
  </div>
)

export const FanBar = ({ label, pct }: { label: Key; pct: number }) => (
  <div class="my-2.5 grid grid-cols-[1fr_2.5rem_1fr] items-center gap-2 text-xs">
    <span>{t(label)}</span>
    <b>{pct}%</b>
    <div class="h-1.5 rounded bg-edge">
      <i class="block size-3 -translate-y-[3px] rounded-full bg-cyan" style={{ marginLeft: `${pct}%` }} />
    </div>
  </div>
)

export const FanSlider = ({ label, pct, onCommit }: { label: Key; pct: number; onCommit: (v: number) => void }) => {
  const [v, setV] = useState(pct)
  useEffect(() => setV(pct), [pct])
  return (
    <div class="my-4 grid grid-cols-[1fr_2.5rem_1fr] items-center gap-2 text-[13px]">
      <span>{t(label)}</span>
      <b>{v}%</b>
      <input
        type="range"
        min="0"
        max="100"
        value={v}
        aria-label={t(label)}
        onInput={e => setV(+e.currentTarget.value)}
        onChange={e => onCommit(Math.round(+e.currentTarget.value))}
      />
    </div>
  )
}

export const Row = ({
  label,
  text,
  value,
  class: c,
}: {
  label?: Key
  text?: string
  value: ComponentChildren
  class?: string
}) => (
  <div class={cn('flex justify-between gap-3 border-b border-edge py-2 text-xs', c)}>
    <span>{text ?? (label && t(label))}</span>
    <b class="min-w-0 text-right [overflow-wrap:anywhere]">{value}</b>
  </div>
)

export const Progress = ({ pct }: { pct: number }) => (
  <div class="flex items-center gap-3">
    <div class="h-3 flex-1 overflow-hidden rounded-full bg-edge">
      <i class="block h-full rounded-full bg-cyan" style={{ width: `${pct}%` }} />
    </div>
    <strong class="text-lg">{Math.round(pct)}%</strong>
  </div>
)

export const Notice = ({ icon = 'info', children }: { icon?: string; children: ComponentChildren }) => (
  <div class="mt-3 flex items-center gap-2.5 rounded-md border border-edge bg-field/60 px-3 py-2.5 text-xs text-muted">
    <Icon n={icon} class="size-4.5 text-cyan" />
    {children}
  </div>
)

export const Warn = ({ children }: { children: ComponentChildren }) => (
  <div class="mt-3 rounded-md border border-amber bg-amber/10 p-2.5 text-xs text-amber">{children}</div>
)

export const Muted = ({ children, class: c }: { children: ComponentChildren; class?: string }) => (
  <p class={cn('text-muted', c)}>{children}</p>
)

// Live MJPEG feed card. The placeholder shows until the first frame; the header only says "Live" while frames arrive, and a dropped stream retries.
export const CameraCard = ({ tall }: { tall?: boolean }) => {
  const [ready, setReady] = useState(false)
  const [failed, setFailed] = useState(false)
  const [src, setSrc] = useState(camera())
  const retry = useRef<number | undefined>(undefined)
  const reload = () => {
    if (retry.current !== undefined) clearTimeout(retry.current)
    retry.current = undefined
    setSrc(camera(`?t=${Date.now()}`))
  }
  const lost = () => {
    setReady(false)
    setFailed(true)
    if (retry.current !== undefined) clearTimeout(retry.current)
    retry.current = window.setTimeout(reload, 2500)
  }
  useEffect(
    () => () => {
      if (retry.current !== undefined) clearTimeout(retry.current)
      retry.current = undefined
      setSrc('')
    },
    []
  )
  return (
    <Card class="flex flex-col">
      <CardHead
        icon="camera"
        title="common.live_camera"
        end={
          ready ? (
            <>
              <Dot /> {t('common.live')}
            </>
          ) : undefined
        }
      />
      <div
        class={cn(
          'relative grid place-items-center overflow-hidden rounded-md border border-edge bg-black',
          tall ? 'min-h-80' : 'aspect-video'
        )}
      >
        {!ready && (
          <div class="text-center text-muted">
            <Icon n="camera" class="mx-auto mb-2 size-10" />
            <strong class="block text-[15px] font-medium">
              {t(tall ? 'common.your_live_print_camera' : 'common.your_cc2_camera_feed')}
            </strong>
            <p class="text-xs">{t(failed ? 'common.camera_unavailable' : 'common.waiting_for_camera_stream')}</p>
            {failed && (
              <Button class="mt-2 text-xs" onClick={reload}>
                {t('common.camera_retry_now')}
              </Button>
            )}
          </div>
        )}
        <img
          src={src}
          alt={t('common.cc2_live_camera')}
          class={cn('absolute inset-0 size-full object-contain', !ready && 'invisible')}
          onLoad={() => {
            setReady(true)
            setFailed(false)
          }}
          onError={lost}
        />
      </div>
      <div class="mt-3 grid gap-2.5 cc2-sm:grid-cols-2">
        <Button
          class="h-auto min-w-0 whitespace-normal px-2 py-2 text-center text-xs cc2-sm:text-sm"
          onClick={() => window.open(camera(), 'cc2-camera')}
        >
          <Icon n="open" class="size-4" />
          {t('common.open_in_new_window')}
        </Button>
        <Button
          class="h-auto min-w-0 whitespace-normal px-2 py-2 text-center text-xs cc2-sm:text-sm"
          onClick={() => window.open(camera(`?snapshot=${Date.now()}`), 'cc2-snapshot')}
        >
          <Icon n="camera" class="size-4" />
          {t('common.snapshot')}
        </Button>
      </div>
    </Card>
  )
}
