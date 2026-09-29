import { useEffect, useRef } from 'preact/hooks'
import { printer, thermalHistory } from '@/lib/state'
import { t } from '@/lib/i18n'

// Series follow the theme's status colours, like the legend dots next to the chart.
const series = { nozzle: '--red', bed: '--blue', chamber: '--amber' }

export const ThermalChart = () => {
  const ref = useRef<HTMLCanvasElement>(null)
  const { rev } = printer.use()
  const draw = () => {
    const canvas = ref.current
    if (!canvas) return
    const box = canvas.getBoundingClientRect()
    if (!box.width || !box.height) return
    const css = getComputedStyle(document.documentElement)
    const dpr = Math.min(devicePixelRatio || 1, 2),
      w = box.width,
      h = box.height,
      ctx = canvas.getContext('2d'),
      pad = { l: 27, r: 7, t: 8, b: 20 }
    if (!ctx) return
    canvas.width = Math.round(w * dpr)
    canvas.height = Math.round(h * dpr)
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, w, h)
    const all = [...thermalHistory.nozzle, ...thermalHistory.bed, ...thermalHistory.chamber]
    const top = Math.max(40, Math.ceil(Math.max(40, ...all) / 20) * 20)
    ctx.font = '10px Segoe UI'
    ctx.lineWidth = 1
    for (let i = 0; i <= 4; i++) {
      const y = pad.t + ((h - pad.t - pad.b) * i) / 4
      ctx.strokeStyle = css.getPropertyValue('--edge')
      ctx.beginPath()
      ctx.moveTo(pad.l, y)
      ctx.lineTo(w - pad.r, y)
      ctx.stroke()
      ctx.fillStyle = css.getPropertyValue('--muted')
      ctx.textAlign = 'right'
      ctx.fillText(String(Math.round(top - (top * i) / 4)), pad.l - 5, y + 3)
    }
    for (const key of Object.keys(series) as (keyof typeof series)[]) {
      const s = thermalHistory[key]
      if (s.length < 2) continue
      ctx.strokeStyle = css.getPropertyValue(series[key])
      ctx.lineWidth = 1.8
      ctx.beginPath()
      s.forEach((v, i) => {
        const x = pad.l + ((w - pad.l - pad.r) * i) / Math.max(1, s.length - 1),
          y = pad.t + ((h - pad.t - pad.b) * (top - v)) / top
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)
      })
      ctx.stroke()
    }
    ctx.fillStyle = css.getPropertyValue('--muted')
    ctx.textAlign = 'right'
    ctx.fillText(t('common.now'), w - pad.r, h - 4)
  }
  useEffect(draw, [rev])
  useEffect(() => {
    addEventListener('resize', draw)
    return () => removeEventListener('resize', draw)
  }, [])
  return <canvas ref={ref} class="block h-32 w-full" aria-label={t('common.temperature_history')} />
}
