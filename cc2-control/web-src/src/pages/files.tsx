import { useEffect, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { FileText, Play, RefreshCw, Search, Trash2, Upload as UploadIcon } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead, Page } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input, Select } from '@/components/ui/field'
import { Row } from '@/components/shared'
import { errText, notify, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { duration, fileSize } from '@/lib/format'
import { startFile } from '@/pages/print-dialog'

type Entry = { storage: 'internal' | 'usb'; file: any }
const key = (e: { storage: string; path?: string; file?: any }) => `${e.storage}\n${e.path ?? e.file.path}`
const where = (s: string) => t(s === 'usb' ? 'files.usb_drive' : 'files.internal_memory')
const I = { size: 16, strokeWidth: 1 }
const metaCache = new Map<string, any>()
const body = (s: string, p: string) => ({
  method: 'POST',
  headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
  body: `${s}\n${p}`,
  cache: 'no-store' as const,
})
const failure = (r: Response) =>
  r
    .json()
    .then(j => String(j.error || `HTTP ${r.status}`))
    .catch(() => `HTTP ${r.status}`)

export const Files = () => {
  const [entries, setEntries] = useState<Entry[]>([])
  const [q, setQ] = useState(''),
    [loc, setLoc] = useState('all'),
    [sort, setSort] = useState('newest')
  const [checked, setChecked] = useState<Set<string>>(new Set())
  const [sel, setSel] = useState<Entry | null>(null)
  const [busy, setBusy] = useState(false),
    [loaded, setLoaded] = useState(false)

  const refresh = async (announce = false) => {
    try {
      const data = await request('/api/gcode-files'),
        next: Entry[] = []
      for (const [storage, group] of [
        ['internal', data.internal],
        ['usb', data.usb],
      ] as const)
        for (const file of group?.files || []) next.push({ storage, file })
      setEntries(next)
      setLoaded(true)
      if (announce) notify(t('files.protected_file_list_refreshed'))
    } catch (e) {
      notify(tpl('files.file_list_unavailable_error', { error: errText(e) }), 'error')
    }
  }
  useEffect(() => {
    refresh()
  }, [])

  const visible = entries
    .filter(e => (loc === 'all' || e.storage === loc) && e.file.path.toLowerCase().includes(q.trim().toLowerCase()))
    .sort((a, b) =>
      sort === 'name'
        ? a.file.path.localeCompare(b.file.path)
        : sort === 'size'
          ? Number(b.file.size || 0) - Number(a.file.size || 0)
          : Number(b.file.modified || 0) - Number(a.file.modified || 0)
    )
  const toggle = (k: string, on: boolean) =>
    setChecked(s => {
      const n = new Set(s)
      on ? n.add(k) : n.delete(k)
      return n
    })
  const items = () =>
    [...checked]
      .map(k => {
        const i = k.indexOf('\n')
        return { storage: k.slice(0, i), path: k.slice(i + 1) }
      })
      .filter(i => i.storage && i.path)

  const guard = () => {
    if (busy) {
      notify(t('files.finish_the_current_file_operation'), 'error')
      return true
    }
    return false
  }
  const remove = async (e: Entry) => {
    const { storage } = e,
      path = e.file.path
    if (
      guard() ||
      !(await ask(`${tpl('files.permanently_delete_this_g_code', { where: where(storage) })}\n\n${path}`, true))
    )
      return
    try {
      const r = await fetch('/api/gcode-files/delete', body(storage, path))
      if (!r.ok) throw Error(await failure(r))
      if (sel && key(sel) === key(e)) setSel(null)
      notify(tpl('files.deleted_path_from_where', { path, where: where(storage) }))
      await refresh()
    } catch (err) {
      notify(tpl('files.delete_failed_error', { error: errText(err) }), 'error')
    }
  }
  const bulk = async (kind: 'delete' | 'copy') => {
    const list = items()
    if (!list.length) return
    let dest = ''
    if (kind === 'delete') {
      if (!(await ask(tpl('files.permanently_delete_n_selected_g', { n: list.length }), true))) return
    } else {
      const d = new Set(list.map(i => (i.storage === 'usb' ? 'internal' : 'usb')))
      if (d.size !== 1) return notify(t('files.select_files_from_only_one_storage'), 'error')
      dest = [...d][0]
      if (!(await ask(tpl('files.copy_n_selected_files_to_where', { n: list.length, where: where(dest) })))) return
    }
    let done = 0
    for (const i of list) {
      try {
        const r =
          kind === 'delete'
            ? await fetch('/api/gcode-files/delete', body(i.storage, i.path))
            : await fetch('/api/gcode-files/copy', body(i.storage, `${dest}\n${i.path}`))
        if (!r.ok) throw Error(await failure(r))
        if (kind === 'delete') toggle(key(i), false)
        done++
      } catch (err) {
        notify(tpl('files.stopped_after_n_files_error', { n: done, error: errText(err) }), 'error')
        break
      }
    }
    await refresh()
    if (done === list.length)
      notify(tpl(kind === 'delete' ? 'files.deleted_n_selected_files' : 'files.copied_n_selected_files', { n: done }))
  }

  const cols =
    'grid grid-cols-[minmax(0,1fr)_auto] items-center gap-2 border-b border-edge px-3 py-2 cc2-md:grid-cols-[1.6fr_.55fr_.75fr_.75fr_1.25fr]'
  return (
    <Page title="common.files" sub="files.browse_protected_g_code_storage_on">
      <div class="grid grid-cols-[minmax(0,1fr)] gap-3.5 cc2-xl:grid-cols-[minmax(0,2.5fr)_minmax(330px,.95fr)]">
        <div class="grid min-w-0 content-start gap-3">
          <Card>
            <div class="grid grid-cols-[minmax(0,1fr)_minmax(0,1fr)_auto] gap-2.5 cc2-md:grid-cols-[1.6fr_.8fr_1fr_auto]">
              <div class="relative col-span-3 cc2-md:col-span-1">
                <Search {...I} class="pointer-events-none absolute left-2.5 top-1/2 -translate-y-1/2 text-muted" />
                <Input
                  class="pl-8"
                  placeholder={t('files.search_files')}
                  value={q}
                  onInput={e => setQ(e.currentTarget.value)}
                />
              </div>
              <Select value={loc} onChange={e => setLoc(e.currentTarget.value)}>
                <option value="all">{t('files.all_files')}</option>
                <option value="internal">{t('files.internal_memory')}</option>
                <option value="usb">{t('files.usb_drive')}</option>
              </Select>
              <Select value={sort} onChange={e => setSort(e.currentTarget.value)}>
                <option value="newest">{t('files.newest_first')}</option>
                <option value="name">{t('common.name')}</option>
                <option value="size">{t('files.size')}</option>
              </Select>
              <Button onClick={() => refresh(true)} aria-label={t('files.refresh')}>
                <RefreshCw {...I} />
              </Button>
            </div>
            {checked.size > 0 && (
              <div class="my-3 flex items-center gap-2">
                <strong>{tpl('files.n_selected', { n: checked.size })}</strong>
                <Button onClick={() => bulk('copy')}>{t('files.copy_selected')}</Button>
                <Button variant="danger" onClick={() => bulk('delete')}>
                  {t('files.delete_selected')}
                </Button>
              </div>
            )}
            <div class="mt-3.5 overflow-hidden rounded-lg border border-edge">
              <div class={cn(cols, 'min-h-11 text-muted')}>
                <span>
                  <input
                    type="checkbox"
                    title={t('files.select_all_shown_files')}
                    checked={visible.length > 0 && visible.every(e => checked.has(key(e)))}
                    onChange={e => {
                      for (const x of visible) toggle(key(x), e.currentTarget.checked)
                    }}
                  />{' '}
                  {t('common.name')}
                </span>
                <span class="hidden cc2-md:inline">{t('files.size')}</span>
                <span class="hidden cc2-md:inline">{t('files.modified')}</span>
                <span class="hidden cc2-md:inline">{t('files.storage')}</span>
                <span>{t('files.actions')}</span>
              </div>
              {visible.map(e => (
                // biome-ignore lint/a11y/noStaticElementInteractions lint/a11y/useKeyWithClickEvents: whole-row click is a mouse shortcut; keyboard users select through the file name button
                <div
                  key={key(e)}
                  onClick={() => setSel(e)}
                  class={cn(
                    cols,
                    'cc2-file-row min-h-16 cursor-pointer',
                    sel && key(sel) === key(e) && 'bg-field outline outline-1 -outline-offset-1 outline-cyan'
                  )}
                >
                  <div class="cc2-file-identity flex min-w-0 items-center gap-2.5 font-semibold">
                    <input
                      type="checkbox"
                      title={t('files.select_file')}
                      checked={checked.has(key(e))}
                      onClick={ev => ev.stopPropagation()}
                      onChange={ev => toggle(key(e), ev.currentTarget.checked)}
                    />
                    <FileText {...I} class="shrink-0" />
                    <button
                      type="button"
                      class="cc2-file-name min-w-0 text-left font-semibold [overflow-wrap:anywhere] hover:underline"
                      title={e.file.path}
                      onClick={() => setSel(e)}
                    >
                      {e.file.path}
                    </button>
                  </div>
                  <span class="hidden cc2-md:inline">{fileSize(e.file.size)}</span>
                  <span class="hidden cc2-md:inline">
                    {Number(e.file.modified) > 0 ? new Date(Number(e.file.modified) * 1000).toLocaleString() : '—'}
                  </span>
                  <span class="hidden cc2-md:inline">{e.storage === 'usb' ? 'USB' : t('files.internal')}</span>
                  <div class="flex gap-1.5">
                    <Button
                      class="px-2.5"
                      onClick={ev => {
                        ev.stopPropagation()
                        startFile(e.storage, e.file.path)
                      }}
                    >
                      <Play {...I} />
                      {t('files.print')}
                    </Button>
                    <Button
                      class="hidden px-2.5 cc2-sm:inline-flex"
                      variant="danger"
                      onClick={ev => {
                        ev.stopPropagation()
                        remove(e)
                      }}
                    >
                      {t('common.delete')}
                    </Button>
                  </div>
                  {sel && key(sel) === key(e) && <div class="cc2-file-expanded col-span-2 hidden">{e.file.path}</div>}
                </div>
              ))}
              {!visible.length && (
                <div class="p-4 text-muted">{t(loaded ? 'files.no_matching_g_code_files' : 'files.loading_files')}</div>
              )}
            </div>
          </Card>
          <div>
            <Upload busy={busy} setBusy={setBusy} refresh={refresh} />
          </div>
        </div>
        <Detail entry={sel} onPrint={e => startFile(e.storage, e.file.path)} onDelete={remove} />
      </div>
    </Page>
  )
}

const Detail = ({
  entry,
  onPrint,
  onDelete,
}: {
  entry: Entry | null
  onPrint: (e: Entry) => void
  onDelete: (e: Entry) => void
}) => {
  const [thumb, setThumb] = useState(''),
    [meta, setMeta] = useState<any>(null)
  const k = entry ? `${key(entry)}\n${Number(entry.file.size || 0)}\n${Number(entry.file.modified || 0)}` : ''
  useEffect(() => {
    setThumb('')
    setMeta(null)
    if (!entry) return
    let dead = false,
      url = ''
    const { storage, file } = entry
    ;(async () => {
      try {
        const r = await fetch('/api/gcode-files/thumbnail', body(storage, file.path))
        const blob = await r.blob()
        if (!r.ok || !blob.type.startsWith('image/')) throw Error('no thumbnail')
        url = URL.createObjectURL(blob)
        if (!dead) setThumb(url)
      } catch {
        /* the placeholder stays */
      }
    })()
    ;(async () => {
      try {
        let m = metaCache.get(k)
        if (!m) {
          m = await request('/api/gcode-files/metadata', body(storage, file.path))
          metaCache.set(k, m)
        }
        if (!dead) setMeta(m)
      } catch {
        /* details stay empty */
      }
    })()
    return () => {
      dead = true
      if (url) URL.revokeObjectURL(url)
    }
  }, [k])
  const ok = (v: any) => v !== null && v !== undefined && Number.isFinite(Number(v)) && Number(v) >= 0
  const temp = (v: any, max: number) => (ok(v) && Number(v) <= max ? `${Math.round(Number(v))} °C` : '—')
  return (
    <Card class="flex flex-col">
      <div class="flex min-w-0 items-center gap-3.5">
        <FileText size={36} strokeWidth={1} class="shrink-0" />
        <div class="min-w-0">
          <h3 class="cc2-file-detail-title text-base leading-snug [overflow-wrap:anywhere]">
            {entry ? entry.file.path : t('files.no_file_selected')}
          </h3>
          <small class="text-muted">{entry ? where(entry.storage) : t('files.select_a_g_code_file_to')}</small>
        </div>
      </div>
      <div class="my-4 grid min-h-56 place-items-center overflow-hidden rounded-lg border border-edge">
        {thumb ? (
          <img src={thumb} alt={t('files.g_code_model_preview')} class="max-h-72 w-full object-contain p-2.5" />
        ) : (
          <FileText size={56} strokeWidth={1} class="text-edge" />
        )}
      </div>
      <div>
        <Row label="files.size" value={entry ? fileSize(entry.file.size) : '—'} />
        <Row
          label="files.layers"
          value={meta && ok(meta.layers) && Number(meta.layers) > 0 ? Math.round(Number(meta.layers)) : '—'}
        />
        <Row
          label="files.estimated_print_time"
          value={meta && ok(meta.estimated_seconds) ? duration(meta.estimated_seconds) : '—'}
        />
        <Row
          label="files.filament_est"
          value={meta && ok(meta.filament_grams) ? `${Number(meta.filament_grams).toFixed(1)} g` : '—'}
        />
        <Row label="files.nozzle_temperature" value={meta ? temp(meta.nozzle_temperature, 500) : '—'} />
        <Row label="files.bed_temperature" value={meta ? temp(meta.bed_temperature, 200) : '—'} />
      </div>
      <Button wide variant="primary" class="mt-4" disabled={!entry} onClick={() => entry && onPrint(entry)}>
        <Play {...I} />
        {t('files.start_protected_print')}
      </Button>
      <Button wide variant="danger" class="mt-2" disabled={!entry} onClick={() => entry && onDelete(entry)}>
        <Trash2 {...I} />
        {t('common.delete')}
      </Button>
      <p class="mt-3 text-center text-muted">{t('files.confirmation_is_required_for')}</p>
    </Card>
  )
}

const Upload = ({
  busy,
  setBusy,
  refresh,
}: {
  busy: boolean
  setBusy: (b: boolean) => void
  refresh: () => Promise<void>
}) => {
  const pick = useRef<HTMLInputElement>(null)
  const [storage, setStorage] = useState('internal'),
    [pct, setPct] = useState(-1),
    [over, setOver] = useState(false),
    [note, setNote] = useState(t('files.maximum_64_mib_no_automatic'))
  const go = async () => {
    const file = pick.current?.files?.[0]
    if (busy || !file) return notify(t('files.choose_a_g_code_file_first'), 'error')
    if (
      !file.name ||
      new TextEncoder().encode(file.name).length >= 256 ||
      !/\.gcode$/i.test(file.name) ||
      file.size === 0 ||
      file.size > 64 * 1024 * 1024
    )
      return notify(t('files.choose_a_valid_gcode_file_between'), 'error')
    if (
      !(await ask(
        `${tpl('files.upload_name_size_to_where', { name: file.name, size: fileSize(file.size), where: where(storage) })}\n\n${t('files.the_file_will_not_be_printed')}`
      ))
    )
      return
    setBusy(true)
    setPct(0)
    setNote(tpl('files.uploading_n', { n: 0 }))
    try {
      await new Promise<void>((resolve, reject) => {
        const x = new XMLHttpRequest()
        x.open(
          'POST',
          `/api/gcode-files/upload?storage=${encodeURIComponent(storage)}&name=${encodeURIComponent(file.name)}`
        )
        x.setRequestHeader('Content-Type', 'application/octet-stream')
        x.timeout = 180000
        x.upload.onprogress = ev => {
          if (ev.lengthComputable) {
            const p = Math.min(100, Math.round((ev.loaded * 100) / ev.total))
            setPct(p)
            setNote(tpl('files.uploading_n', { n: p }))
          }
        }
        x.onerror = () => reject(Error(t('files.network_error')))
        x.ontimeout = () => reject(Error(t('files.upload_timed_out')))
        x.onload = () => {
          if (x.status === 201) resolve()
          else {
            let m = `HTTP ${x.status}`
            try {
              m = JSON.parse(x.responseText).error || m
            } catch {
              /* keep HTTP status */
            }
            reject(Error(m))
          }
        }
        x.send(file)
      })
      if (pick.current) pick.current.value = ''
      setPct(100)
      setNote(t('files.upload_completed_file_is_ready_in'))
      notify(tpl('files.name_uploaded_to_where', { name: file.name, where: where(storage) }))
      await refresh()
    } catch (e) {
      const m = tpl('files.upload_failed_error', { error: errText(e) })
      setNote(m)
      notify(m, 'error')
    } finally {
      setBusy(false)
    }
  }
  return (
    <Card>
      <CardHead title="files.upload_file" end={t('files.local_upload')} />
      {/* biome-ignore lint/a11y/noStaticElementInteractions: drag and drop is a pointer shortcut; the file input and button are the keyboard path */}
      <div
        class={cn(
          'grid grid-cols-[minmax(0,1fr)] justify-items-center gap-2.5 rounded-lg border border-dashed border-cyan p-3 transition-colors',
          over && 'bg-cyan/10'
        )}
        onDragOver={e => {
          if (busy) return
          e.preventDefault()
          setOver(true)
        }}
        onDragLeave={() => setOver(false)}
        onDrop={e => {
          e.preventDefault()
          setOver(false)
          const f = e.dataTransfer?.files
          if (busy || !f?.length || !pick.current) return
          pick.current.files = f
          void go()
        }}
      >
        <input
          ref={pick}
          type="file"
          accept=".gcode"
          class="w-full min-w-0"
          disabled={busy}
          aria-label={t('files.select_g_code_file')}
        />
        <label class="flex items-center gap-2">
          {t('files.destination')}{' '}
          <Select class="w-auto" value={storage} disabled={busy} onChange={e => setStorage(e.currentTarget.value)}>
            <option value="internal">{t('files.internal_memory')}</option>
            <option value="usb">{t('files.usb_drive')}</option>
          </Select>
        </label>
        <Button variant="primary" disabled={busy} onClick={go}>
          <UploadIcon {...I} />
          {t('files.upload_g_code')}
        </Button>
        <small class="text-muted">{t('files.or_drop_a_gcode_file_here')}</small>
        {pct >= 0 && <progress class="w-full" value={pct} max="100" />}
        <small class="text-center text-muted">{note}</small>
      </div>
    </Card>
  )
}
