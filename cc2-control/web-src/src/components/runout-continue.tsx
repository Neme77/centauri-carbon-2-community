import { useEffect, useRef, useState } from 'preact/hooks'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { printer } from '@/lib/state'
import { t, tpl } from '@/lib/i18n'
import { ask } from '@/lib/confirm'
import { errText, notify, post, request } from '@/lib/api'
import { poll } from '@/lib/poll'

type Slot = { slot: number; status: number; type: string; color: string; eligible: boolean; same_material: boolean }
type Runout = {
  available: boolean
  reason: string
  match_material: boolean
  job: {
    state: 'idle' | 'running' | 'done' | 'failed'
    slot: number
    from: number
    automatic: boolean
    message: string
    auto_note: string
  }
  slot?: number
  tools?: number[]
  material?: string
  slots?: Slot[]
}

// A Canvas runout pause (issue #284): the firmware's own retry of the empty slot has failed. Shown on every
// page while the print waits; pick another loaded slot and the printer loads it, purges and resumes.
export const RunoutContinue = () => {
  const { data, ok } = printer.use()
  const paused = Boolean(ok && data?.print?.state === 'paused')
  const [state, setState] = useState<Runout | null>(null)
  const [busy, setBusy] = useState(false)
  const announced = useRef(false)
  useEffect(() => {
    if (!paused) {
      setState(null)
      return
    }
    const read = async () => setState(await request('/api/runout'))
    return poll(read, 3000)
  }, [paused])
  const job = state?.job
  const running = job?.state === 'running'
  const show = Boolean(paused && state && (state.available || running || job?.state === 'failed'))
  useEffect(() => {
    if (show && state?.available && !announced.current) notify(tpl('runout.title', { slot: (state.slot ?? 0) + 1 }))
    announced.current = Boolean(show && state?.available)
  }, [show, state?.available])
  if (!show || !state) return null
  const empty = (state.slot ?? -1) + 1
  // Same material first: the safe choice; the others ask with a warning.
  const choices = (state.slots || [])
    .filter(s => s.eligible)
    .sort((a, b) => Number(b.same_material) - Number(a.same_material))
  const choose = async (s: Slot) => {
    const question = s.same_material
      ? tpl('runout.confirm', { slot: s.slot + 1 })
      : tpl('runout.confirm_mismatch', { slot: s.slot + 1, material: s.type || '?', expected: state.material || '?' })
    if (!(await ask(question))) return
    setBusy(true)
    try {
      await post('/api/runout/continue', `slot=${s.slot}`)
      notify(tpl('runout.loading', { slot: s.slot + 1 }))
      setState(await request('/api/runout'))
    } catch (e) {
      notify(tpl('runout.failed', { error: errText(e) }), 'error')
    } finally {
      setBusy(false)
    }
  }
  return (
    <Card
      role="region"
      aria-label={t('runout.heading')}
      data-runout-continue
      class="fixed right-3 bottom-3 z-40 w-[min(440px,calc(100vw-24px))] shadow-2xl"
    >
      <CardHead icon="canvas" title="runout.heading" />
      {running ? (
        <p class="text-muted">{tpl('runout.loading', { slot: (job?.slot ?? 0) + 1 })}</p>
      ) : (
        <>
          {state.available && (
            <p class="text-muted">
              {tpl('runout.detail', {
                slot: empty,
                material: state.material || '?',
                tools: (state.tools || []).map(n => `T${n}`).join(', '),
              })}
            </p>
          )}
          {job?.state === 'failed' && (
            <p class="mt-2 text-red">{tpl('runout.failed', { error: job.message || '?' })}</p>
          )}
          {job?.auto_note === 'no_matching_slot' && <p class="mt-2 text-xs text-muted">{t('runout.no_match_auto')}</p>}
          {state.available && !choices.length && <p class="mt-2 text-muted">{t('runout.none_loaded')}</p>}
          {state.available && choices.length > 0 && (
            <div class="mt-3 grid gap-2">
              {choices.map(s => (
                <Button key={s.slot} wide class="justify-between" disabled={busy} onClick={() => choose(s)}>
                  <span class="flex items-center gap-2">
                    <span
                      aria-hidden="true"
                      class="inline-block h-3.5 w-3.5 rounded-full border border-white/30"
                      style={{ background: s.color || 'transparent' }}
                    />
                    {tpl('runout.continue_from', { slot: s.slot + 1 })}
                    <span class="text-muted">{s.type}</span>
                  </span>
                  <small class={s.same_material ? 'text-cyan' : 'text-amber'}>
                    {t(s.same_material ? 'runout.same_material' : 'runout.other_material')}
                  </small>
                </Button>
              ))}
            </div>
          )}
        </>
      )}
    </Card>
  )
}
