import type { ComponentChildren } from 'preact'
import { useEffect, useState } from 'preact/hooks'
import { cn } from '@/lib/utils'
import { Dot } from '@/components/ui/badge'
import { Icon } from '@/components/icons'
import { t } from '@/lib/i18n'
import { camera } from '@/lib/state'
import { Button } from '@/components/ui/button'

export const Reading = ({ dot, label, value, target }: { dot?: 'red' | 'blue' | 'amber'; label: string; value: string; target?: string }) => (
  <div class="flex items-center gap-2 text-xs">{dot && <Dot c={dot} />}{t(label)}<b class="ml-auto text-[13px] font-semibold">{value}</b>{target && <span class="min-w-6 text-right text-[11px] text-muted">{target}</span>}</div>
)

export const FanBar = ({ label, pct }: { label: string; pct: number }) => (
  <div class="my-2.5 grid grid-cols-[1fr_2.5rem_1fr] items-center gap-2 text-xs">
    <span>{t(label)}</span><b>{pct}%</b>
    <div class="h-1.5 rounded bg-edge"><i class="block size-3 -translate-y-[3px] rounded-full bg-cyan" style={{ marginLeft: `${pct}%` }} /></div>
  </div>
)

export const FanSlider = ({ label, pct, onCommit }: { label: string; pct: number; onCommit: (v: number) => void }) => {
  const [v, setV] = useState(pct)
  useEffect(() => setV(pct), [pct])
  return (
    <div class="my-4 grid grid-cols-[1fr_2.5rem_1fr] items-center gap-2 text-[13px]">
      <span>{t(label)}</span><b>{v}%</b>
      <input type="range" min="0" max="100" value={v} aria-label={t(label)} onInput={e => setV(+e.currentTarget.value)} onChange={e => onCommit(Math.round(+e.currentTarget.value))} />
    </div>
  )
}

export const Row = ({ label, value, class: c }: { label: string; value: ComponentChildren; class?: string }) => (
  <div class={cn('flex justify-between gap-3 border-b border-edge py-2 text-xs', c)}><span>{t(label)}</span><b class="min-w-0 text-right [overflow-wrap:anywhere]">{value}</b></div>
)

export const Progress = ({ pct }: { pct: number }) => (
  <div class="flex items-center gap-3">
    <div class="h-3 flex-1 overflow-hidden rounded-full bg-edge"><i class="block h-full rounded-full bg-cyan" style={{ width: `${pct}%` }} /></div>
    <strong class="text-lg">{Math.round(pct)}%</strong>
  </div>
)

export const Notice = ({ icon = 'info', children }: { icon?: string; children: ComponentChildren }) => (
  <div class="mt-3 flex items-center gap-2.5 rounded-md border border-edge bg-field/60 px-3 py-2.5 text-xs text-muted"><Icon n={icon} class="size-4.5 text-cyan" />{children}</div>
)

export const Warn = ({ children }: { children: ComponentChildren }) => (
  <div class="mt-3 rounded-md border border-amber bg-amber/10 p-2.5 text-xs text-amber">{children}</div>
)

export const Muted = ({ children, class: c }: { children: ComponentChildren; class?: string }) => <p class={cn('text-muted', c)}>{children}</p>

// Live MJPEG feed with the same retry behaviour as before; the placeholder shows until the first frame.
export const CameraCard = ({ tall }: { tall?: boolean }) => {
  const [ready, setReady] = useState(false)
  const [src, setSrc] = useState(camera())
  return (
    <>
      <div class={cn('relative grid place-items-center overflow-hidden rounded-md border border-edge bg-black', tall ? 'min-h-80' : 'aspect-video')}>
        {!ready && <div class="text-center text-muted"><Icon n="camera" class="mx-auto mb-2 size-10" /><strong class="block text-[15px] font-medium">{t(tall ? 'Your live print camera' : 'Your CC2 camera feed')}</strong><p class="text-xs">{t('Waiting for camera stream')}</p></div>}
        <img src={src} alt={t('CC2 live camera')} class={cn('absolute inset-0 size-full object-contain', !ready && 'invisible')} onLoad={() => setReady(true)} onError={() => setTimeout(() => setSrc(camera(`?t=${Date.now()}`)), 2500)} />
      </div>
      <div class="mt-3 grid gap-2.5 sm:grid-cols-2">
        <Button class="h-auto min-w-0 whitespace-normal px-2 py-2 text-center text-xs sm:text-sm" onClick={() => window.open(camera(), 'cc2-camera')}><Icon n="open" class="size-4" />{t('Open in new window')}</Button>
        <Button class="h-auto min-w-0 whitespace-normal px-2 py-2 text-center text-xs sm:text-sm" onClick={() => window.open(camera(`?snapshot=${Date.now()}`), 'cc2-snapshot')}><Icon n="camera" class="size-4" />{t('Snapshot')}</Button>
      </div>
    </>
  )
}
