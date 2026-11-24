import { useEffect, useRef, useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { Card, CardHead } from '@/components/ui/card'
import { Dialog, DialogTitle } from '@/components/ui/dialog'
import { Select } from '@/components/ui/field'
import { Notice } from '@/components/shared'
import { notify } from '@/lib/api'
import { ask } from '@/lib/confirm'
import { i18n, type Key, t, tpl } from '@/lib/i18n'
import { ls } from '@/lib/store'
import {
  discardHeld,
  type HeldEntry,
  keepOldConnections,
  loadSpoolmanCatalog,
  refreshSpools,
  sendHeld,
  type SpoolLibrary,
  type SpoolmanCatalog,
  spools,
  refreshSpoolmanStatus,
  spoolmanState,
  unlinkOldConnections,
} from '@/lib/spools'

// The list of usage that was not sent to Spoolman. Every entry is the user's to decide: nothing here is sent by itself.
type Act = (fn: () => Promise<unknown>) => Promise<void>

const REASONS: Record<HeldEntry['reason'], Key> = {
  login_changed: 'spools.held_reason_login_changed',
  server_changed: 'spools.held_reason_server_changed',
  not_found: 'spools.held_reason_not_found',
  unsure: 'spools.held_reason_unsure',
}
const SEEN = 'cc2-spoolman-held-seen'
const grams = (n: number) => n.toFixed(1)
// In the language of the page, not the browser's.
const date = (s: number) =>
  s > 0 ? new Date(s * 1000).toLocaleDateString(i18n.get().lang, { day: 'numeric', month: 'short' }) : '—'
const sleep = (ms: number) => new Promise(done => setTimeout(done, ms))

// What the page has to say once after a change of server or login: the newest such entry the user has not looked at yet.
const newestChange = (entries: HeldEntry[]) =>
  entries.reduce((n, e) => (e.reason === 'login_changed' || e.reason === 'server_changed' ? Math.max(n, e.id) : n), 0)
const seen = () => Number(ls.get(SEEN)) || 0

// The page mounts this with `key={lib.spoolman_gen}`: after a change of the Spoolman address or login it is a new
// instance, so the catalog, the ticks and the chosen spools of the old server are gone at once, and an answer that was
// still on its way for the old server finds nobody to fill. `gen` is constant for the life of an instance.
export const SpoolmanHeld = ({ lib, busy, act }: { lib: SpoolLibrary; busy: boolean; act: Act }) => {
  const entries = lib.spoolman_held ?? []
  const stale = lib.spoolman_stale_links ?? 0
  const gen = lib.spoolman_gen
  const alive = useRef(true)
  useEffect(
    () => () => {
      alive.current = false
    },
    []
  )
  const [picked, setPicked] = useState<Record<number, boolean>>({})
  const [chosen, setChosen] = useState<Record<number, string>>({})
  const [catalog, setCatalog] = useState<SpoolmanCatalog | null>(null)
  const [noticeFor, setNoticeFor] = useState(0)
  const asked = useRef(false)
  const reading = useRef(false)
  const lastSync = useRef(0) // the printer's last sync when the read of the spools failed
  const retries = useRef(0)
  const sendable = entries.filter(e => e.reason !== 'unsure')
  const noSpools = !!catalog && !catalog.spools.length

  const read = async () => {
    reading.current = true
    const got = await loadSpoolmanCatalog().catch(
      () => ({ loading: false, error: 'x', spools: [], gen }) as SpoolmanCatalog
    )
    if (!alive.current) return
    if (got.gen !== gen) {
      // The printer's settings changed after this page last looked: this is another server's list. Look again; the
      // new generation replaces this instance.
      reading.current = false
      void refreshSpools()
      return
    }
    if (got.spools.length) retries.current = 0
    else {
      await refreshSpoolmanStatus()
      lastSync.current = spoolmanState.get().data?.last_sync ?? 0
    }
    if (!alive.current) return
    reading.current = false
    setCatalog(got)
  }
  // Spoolman's spools for the choice next to each entry: read when there is something to choose for.
  useEffect(() => {
    if (!sendable.length || asked.current) return
    asked.current = true
    void read()
  }, [sendable.length])
  // Spoolman could not be read: ask again after the printer's next good sync, never more than three tries in a row (every
  // try counts, whether or not it works), so the choice fills itself without a reload. The sync is read from the one status
  // the Spoolman card polls (every 15 s), not with a request of its own for every refresh of the page.
  const sync = spoolmanState.use().data
  const syncTime = sync?.result === 'ok' ? sync.last_sync : 0
  useEffect(() => {
    if (!noSpools || !sendable.length || reading.current || retries.current >= 3) return
    if (syncTime <= lastSync.current) return
    retries.current++
    lastSync.current = syncTime
    void read()
  }, [syncTime, noSpools, catalog])
  // The notice after a change: once per change, until "Review" is pressed.
  const newest = newestChange(entries)
  useEffect(() => {
    if (newest > seen()) setNoticeFor(newest)
  }, [newest])

  if (!entries.length && !stale) return null
  const target = (e: HeldEntry) => chosen[e.id] ?? (e.reason === 'login_changed' ? e.spoolman_id : '')
  const picks = entries.filter(e => picked[e.id] && e.reason !== 'unsure' && !e.sending)
  const missing = picks.some(e => !target(e))
  const total = picks.reduce((n, e) => n + e.grams, 0)
  const count = entries.length
  const review = () => {
    ls.set(SEEN, String(noticeFor))
    setNoticeFor(0)
    document.getElementById('spoolman-held')?.scrollIntoView({ block: 'start' })
  }

  const send = () =>
    act(async () => {
      const ids = picks.map(e => e.id)
      try {
        await sendHeld(Object.fromEntries(picks.map(e => [String(e.id), target(e)])), gen)
      } catch (e) {
        // Refused because the address or login changed since this list was loaded: say so in the page's language.
        await refreshSpools()
        if (spools.get().data?.spoolman_gen === gen) throw e
        notify(t('spools.held_settings_changed'), 'error')
        return
      }
      if (alive.current) setPicked({})
      // The request runs on the printer; the list shows it as sending until Spoolman has answered.
      for (let i = 0; i < 220; i++) {
        await sleep(700)
        await refreshSpools()
        const now = spools.get().data?.spoolman_held ?? []
        if (!ids.some(id => now.find(e => e.id === id)?.sending)) break
      }
      const left = (spools.get().data?.spoolman_held ?? []).filter(e => ids.includes(e.id)).length
      notify(t(left ? 'spools.held_send_failed' : 'spools.held_sent_toast'), left ? 'error' : 'info')
    })

  const dropEntry = (e: HeldEntry) =>
    act(async () => {
      const unsure = e.reason === 'unsure'
      const text = unsure ? t('spools.held_confirm_remove') : tpl('spools.held_confirm_send', { grams: grams(e.grams) })
      if (!(await ask(text, true, undefined, t(unsure ? 'spools.held_remove' : 'spools.held_dont_send')))) return
      await discardHeld(e.id)
      setPicked(p => ({ ...p, [e.id]: false }))
    })

  const option = (id: string, label: string) => (
    <option key={id} value={id}>
      {label}
    </option>
  )
  const spoolChoice = (e: HeldEntry) => {
    const list = catalog?.spools ?? []
    const known = list.some(s => s.id === target(e))
    return (
      <Select
        class="min-w-0 cc2-sm:w-56"
        aria-label={`${t('spools.held_choose')}: ${e.name || '—'}`}
        disabled={busy || e.sending}
        value={target(e)}
        onChange={ev => setChosen(c => ({ ...c, [e.id]: ev.currentTarget.value }))}
      >
        {option('', t('spools.held_choose'))}
        {target(e) && !known && option(target(e), tpl('spools.held_target_unknown', { id: target(e) }))}
        {list.map(s => option(s.id, `${s.name || s.material} · ${s.material} · ${grams(s.remaining)} g`))}
      </Select>
    )
  }

  return (
    <>
      {noticeFor > 0 && (
        <Dialog onClose={review} width={460}>
          <DialogTitle class="mb-3">{t('spools.held_notice_title')}</DialogTitle>
          <p class="text-[15px] leading-relaxed">{t('spools.held_notice_text')}</p>
          <div class="mt-5 flex justify-end">
            <Button variant="primary" onClick={review} autofocus>
              {t('spools.held_notice_review')}
            </Button>
          </div>
        </Dialog>
      )}
      <Card id="spoolman-held" data-held={count}>
        <CardHead
          icon="spool"
          title={count === 1 ? 'spools.held_title_one' : 'spools.held_title_other'}
          values={{ n: count }}
        />
        <p class="text-[13px] text-muted">{t('spools.held_intro')}</p>
        {stale > 0 && (
          <div
            class="mt-3 rounded-lg border border-amber bg-amber/10 p-3 text-[13px]"
            data-held-stale={stale}
            role="note"
          >
            <div class="flex flex-wrap items-center gap-x-3 gap-y-2">
              <span class="mr-auto text-amber">
                {tpl(stale === 1 ? 'spools.held_stale_one' : 'spools.held_stale_other', { n: stale })}
              </span>
              <Button
                disabled={busy}
                onClick={() =>
                  act(async () => {
                    await keepOldConnections()
                    notify(t('spools.held_kept_toast'))
                  })
                }
              >
                {t('spools.held_keep')}
              </Button>
              <Button
                disabled={busy}
                onClick={() =>
                  act(async () => {
                    await unlinkOldConnections()
                    notify(t('spools.held_unlinked_toast'))
                  })
                }
              >
                {t('spools.held_unlink')}
              </Button>
            </div>
            <p class="mt-1.5 text-xs text-muted">{t('spools.held_keep_note')}</p>
            <p class="mt-1 text-xs text-muted">{t('spools.held_unlink_note')}</p>
          </div>
        )}
        {!entries.length ? (
          <Notice>{t('spools.held_empty')}</Notice>
        ) : (
          <>
            {sendable.length > 0 && (
              <label class="mt-3 flex items-center gap-2 text-[13px] text-muted">
                <input
                  type="checkbox"
                  aria-label={t('spools.held_select_all')}
                  checked={sendable.every(e => picked[e.id])}
                  disabled={busy || sendable.some(e => e.sending)}
                  onChange={ev => setPicked(Object.fromEntries(sendable.map(e => [e.id, ev.currentTarget.checked])))}
                />
                {t('spools.held_select_all')}
                {picks.length > 0 && <span>· {tpl('spools.held_selected', { n: picks.length })}</span>}
              </label>
            )}
            <ul class="mt-2 grid gap-2">
              {entries.map(e => (
                <li
                  key={e.id}
                  class="grid gap-2 rounded-lg border border-edge bg-field/40 p-3 cc2-sm:grid-cols-[auto_minmax(0,1fr)_auto] cc2-sm:items-center"
                  data-held-id={e.id}
                  data-reason={e.reason}
                >
                  {e.reason === 'unsure' ? (
                    <span class="hidden cc2-sm:block cc2-sm:w-4" />
                  ) : (
                    <input
                      type="checkbox"
                      class="justify-self-start"
                      aria-label={`${e.name} ${grams(e.grams)} g`}
                      checked={!!picked[e.id]}
                      disabled={busy || e.sending}
                      onChange={ev => setPicked(p => ({ ...p, [e.id]: ev.currentTarget.checked }))}
                    />
                  )}
                  <div class="min-w-0">
                    <div class="font-semibold [overflow-wrap:anywhere]">
                      <span class="sr-only">{t('spools.held_col_name')}: </span>
                      {e.name || '—'}
                    </div>
                    <div class="text-xs text-muted">
                      <span class="sr-only">{t('spools.held_col_amount')}: </span>
                      {grams(e.grams)} g · <span class="sr-only">{t('spools.held_col_date')}: </span>
                      {date(e.time)}
                    </div>
                    <div class="mt-1 text-xs text-amber">
                      <span class="sr-only">{t('spools.held_col_reason')}: </span>
                      {t(REASONS[e.reason])}
                    </div>
                  </div>
                  <div class="flex flex-wrap items-center gap-2 cc2-sm:justify-end">
                    {e.sending ? (
                      <span class="text-xs text-muted">{t('spools.held_sending')}</span>
                    ) : (
                      <>
                        {e.reason !== 'unsure' && spoolChoice(e)}
                        <Button disabled={busy} onClick={() => dropEntry(e)}>
                          {t(e.reason === 'unsure' ? 'spools.held_remove' : 'spools.held_dont_send')}
                        </Button>
                      </>
                    )}
                  </div>
                </li>
              ))}
            </ul>
            {noSpools && sendable.length > 0 && (
              <p class="mt-2 text-xs text-muted">{t('spools.held_catalog_failed')}</p>
            )}
            {catalog?.truncated && sendable.length > 0 && (
              <p class="mt-2 text-xs text-muted">{t('spools.held_catalog_truncated')}</p>
            )}
            <div class="mt-3 flex flex-wrap items-center gap-3">
              <span class="mr-auto text-xs text-muted">{t('spools.held_later')}</span>
              {missing && <span class="text-xs text-amber">{t('spools.held_send_help')}</span>}
              <Button variant="primary" disabled={busy || !picks.length || missing} onClick={send}>
                {picks.length
                  ? tpl('spools.held_send', { n: picks.length, grams: grams(total) })
                  : t('spools.held_send_none')}
              </Button>
            </div>
          </>
        )}
      </Card>
    </>
  )
}
