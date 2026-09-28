import { useEffect, useRef, useState } from 'preact/hooks'
import { ask } from '@/lib/confirm'
import { FileText, Play, RefreshCw, Search, Trash2, Upload as UploadIcon } from 'lucide-preact'
import { cn } from '@/lib/utils'
import { Card, CardHead } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input, Select } from '@/components/ui/field'
import { Row } from '@/components/shared'
import { errText, notify, post, request } from '@/lib/api'
import { t, tpl } from '@/lib/i18n'
import { duration, fileSize } from '@/lib/format'
import { startFile } from '@/pages/print-dialog'

type Entry = { storage: 'internal' | 'usb'; file: any }
const key = (e: { storage: string; path?: string; file?: any }) => `${e.storage}\n${e.path ?? e.file.path}`
const where = (s: string) => t(s === 'usb' ? 'USB drive' : 'Internal memory')
const I = { size: 16, strokeWidth: 1 }
const metaCache = new Map<string, any>()
const body = (s: string, p: string) => ({ method: 'POST', headers: { 'Content-Type': 'text/plain;charset=UTF-8' }, body: `${s}\n${p}`, cache: 'no-store' as const })
const failure = (r: Response) => r.json().then(j => String(j.error || `HTTP ${r.status}`)).catch(() => `HTTP ${r.status}`)

export const Files = () => {
  const [entries, setEntries] = useState<Entry[]>([])
  const [q, setQ] = useState(''), [loc, setLoc] = useState('all'), [sort, setSort] = useState('newest')
  const [checked, setChecked] = useState<Set<string>>(new Set())
  const [sel, setSel] = useState<Entry | null>(null)
  const [busy, setBusy] = useState(false), [loaded, setLoaded] = useState(false)

  const refresh = async (announce = false) => {
    try {
      const data = await request('/api/gcode-files'), next: Entry[] = []
      for (const [storage, group] of [['internal', data.internal], ['usb', data.usb]] as const) for (const file of group?.files || []) next.push({ storage, file })
      setEntries(next); setLoaded(true)
      if (announce) notify(t('Protected file list refreshed.'))
    } catch (e) { notify(tpl('File list unavailable: {error}', { error: errText(e) })) }
  }
  useEffect(() => { refresh() }, [])

  const visible = entries.filter(e => (loc === 'all' || e.storage === loc) && e.file.path.toLowerCase().includes(q.trim().toLowerCase()))
    .sort((a, b) => sort === 'name' ? a.file.path.localeCompare(b.file.path) : sort === 'size' ? Number(b.file.size || 0) - Number(a.file.size || 0) : Number(b.file.modified || 0) - Number(a.file.modified || 0))
  const toggle = (k: string, on: boolean) => setChecked(s => { const n = new Set(s); on ? n.add(k) : n.delete(k); return n })
  const items = () => [...checked].map(k => { const i = k.indexOf('\n'); return { storage: k.slice(0, i), path: k.slice(i + 1) } }).filter(i => i.storage && i.path)

  const guard = () => { if (busy) { notify(t('Finish the current file operation first.')); return true } return false }
  const remove = async (e: Entry) => {
    const { storage } = e, path = e.file.path
    if (guard() || !(await ask(`${tpl('Permanently delete this G-code from {where}?', { where: where(storage) })}\n\n${path}`, true))) return
    try {
      const r = await fetch('/api/gcode-files/delete', body(storage, path))
      if (!r.ok) throw Error(await failure(r))
      if (sel && key(sel) === key(e)) setSel(null)
      notify(tpl('Deleted {path} from {where}.', { path, where: where(storage) }))
      await refresh()
    } catch (err) { notify(tpl('Delete failed: {error}', { error: errText(err) })) }
  }
  const bulk = async (kind: 'delete' | 'copy') => {
    const list = items()
    if (!list.length) return
    let dest = ''
    if (kind === 'delete') { if (!(await ask(tpl('Permanently delete {n} selected G-code files?', { n: list.length }), true))) return }
    else {
      const d = new Set(list.map(i => (i.storage === 'usb' ? 'internal' : 'usb')))
      if (d.size !== 1) return notify(t('Select files from only one storage location.'))
      dest = [...d][0]
      if (!(await ask(tpl('Copy {n} selected files to {where}?', { n: list.length, where: where(dest) })))) return
    }
    let done = 0
    for (const i of list) {
      try {
        const r = kind === 'delete' ? await fetch('/api/gcode-files/delete', body(i.storage, i.path)) : await fetch('/api/gcode-files/copy', body(i.storage, `${dest}\n${i.path}`))
        if (!r.ok) throw Error(await failure(r))
        if (kind === 'delete') toggle(key(i), false)
        done++
      } catch (err) { notify(tpl('Stopped after {n} files: {error}', { n: done, error: errText(err) })); break }
    }
    await refresh()
    if (done === list.length) notify(tpl(kind === 'delete' ? 'Deleted {n} selected files.' : 'Copied {n} selected files.', { n: done }))
  }

  const cols = 'grid grid-cols-[minmax(0,1fr)_auto] items-center gap-2 border-b border-edge px-3 py-2 md:grid-cols-[1.6fr_.55fr_.75fr_.75fr_1.25fr]'
  return (
    <div class="grid grid-cols-[minmax(0,1fr)] gap-3.5 xl:grid-cols-[minmax(0,2.5fr)_minmax(330px,.95fr)]">
      <div class="grid min-w-0 content-start gap-3">
        <Card>
          <div class="mb-3.5"><h2 class="text-2xl font-semibold">{t('Files')}</h2><p class="text-muted">{t('Browse protected G-code storage on your CC2')}</p></div>
          <div class="grid grid-cols-[minmax(0,1fr)_minmax(0,1fr)_auto] gap-2.5 md:grid-cols-[1.6fr_.8fr_1fr_auto]">
            <div class="relative col-span-3 md:col-span-1"><Search {...I} class="pointer-events-none absolute left-2.5 top-1/2 -translate-y-1/2 text-muted" /><Input class="pl-8" placeholder={t('Search files')} value={q} onInput={e => setQ(e.currentTarget.value)} /></div>
            <Select value={loc} onChange={e => setLoc(e.currentTarget.value)}><option value="all">{t('All files')}</option><option value="internal">{t('Internal memory')}</option><option value="usb">{t('USB drive')}</option></Select>
            <Select value={sort} onChange={e => setSort(e.currentTarget.value)}><option value="newest">{t('Newest first')}</option><option value="name">{t('Name')}</option><option value="size">{t('Size')}</option></Select>
            <Button onClick={() => refresh(true)} aria-label="Refresh"><RefreshCw {...I} /></Button>
          </div>
          {checked.size > 0 && <div class="my-3 flex items-center gap-2"><strong>{tpl('{n} selected', { n: checked.size })}</strong><Button onClick={() => bulk('copy')}>{t('Copy selected')}</Button><Button variant="danger" onClick={() => bulk('delete')}>{t('Delete selected')}</Button></div>}
          <div class="mt-3.5 overflow-hidden rounded-lg border border-edge">
            <div class={cn(cols, 'min-h-11 text-muted')}>
              <span><input type="checkbox" title={t('Select all shown files')} checked={visible.length > 0 && visible.every(e => checked.has(key(e)))} onChange={e => visible.forEach(x => toggle(key(x), e.currentTarget.checked))} /> {t('Name')}</span>
              <span class="hidden md:inline">{t('Size')}</span><span class="hidden md:inline">{t('Modified')}</span><span class="hidden md:inline">{t('Storage')}</span><span>{t('Actions')}</span>
            </div>
            {visible.map(e => (
              <div key={key(e)} onClick={() => setSel(e)} class={cn(cols, 'min-h-16 cursor-pointer', sel && key(sel) === key(e) && 'bg-field outline outline-1 -outline-offset-1 outline-cyan')}>
                <div class="flex min-w-0 items-center gap-2.5 font-semibold"><input type="checkbox" title={t('Select file')} checked={checked.has(key(e))} onClick={ev => ev.stopPropagation()} onChange={ev => toggle(key(e), ev.currentTarget.checked)} /><FileText {...I} class="shrink-0" /><span class="[overflow-wrap:anywhere]">{e.file.path}</span></div>
                <span class="hidden md:inline">{fileSize(e.file.size)}</span>
                <span class="hidden md:inline">{Number(e.file.modified) > 0 ? new Date(Number(e.file.modified) * 1000).toLocaleString() : '—'}</span>
                <span class="hidden md:inline">{e.storage === 'usb' ? 'USB' : t('Internal')}</span>
                <div class="flex gap-1.5" onClick={ev => ev.stopPropagation()}><Button class="px-2.5" onClick={() => startFile(e.storage, e.file.path)}><Play {...I} />{t('Print')}</Button><Button class="hidden px-2.5 sm:inline-flex" variant="danger" onClick={() => remove(e)}>{t('Delete')}</Button></div>
              </div>
            ))}
            {!visible.length && <div class="p-4 text-muted">{t(loaded ? 'No matching G-code files.' : 'Loading files…')}</div>}
          </div>
        </Card>
        <div>
          <Upload busy={busy} setBusy={setBusy} refresh={refresh} />
        </div>
      </div>
      <Detail entry={sel} onPrint={e => startFile(e.storage, e.file.path)} onDelete={remove} />
    </div>
  )
}

const Detail = ({ entry, onPrint, onDelete }: { entry: Entry | null; onPrint: (e: Entry) => void; onDelete: (e: Entry) => void }) => {
  const [thumb, setThumb] = useState(''), [meta, setMeta] = useState<any>(null)
  const k = entry ? `${key(entry)}\n${Number(entry.file.size || 0)}\n${Number(entry.file.modified || 0)}` : ''
  useEffect(() => {
    setThumb(''); setMeta(null)
    if (!entry) return
    let dead = false, url = ''
    const { storage, file } = entry
    ;(async () => {
      try {
        const r = await fetch('/api/gcode-files/thumbnail', body(storage, file.path))
        const blob = await r.blob()
        if (!r.ok || !blob.type.startsWith('image/')) throw Error('no thumbnail')
        url = URL.createObjectURL(blob)
        if (!dead) setThumb(url)
      } catch { /* the placeholder stays */ }
    })()
    ;(async () => {
      try {
        let m = metaCache.get(k)
        if (!m) { m = await request('/api/gcode-files/metadata', body(storage, file.path)); metaCache.set(k, m) }
        if (!dead) setMeta(m)
      } catch { /* details stay empty */ }
    })()
    return () => { dead = true; if (url) URL.revokeObjectURL(url) }
  }, [k])
  const ok = (v: any) => v !== null && v !== undefined && Number.isFinite(Number(v)) && Number(v) >= 0
  const temp = (v: any, max: number) => (ok(v) && Number(v) <= max ? `${Math.round(Number(v))} °C` : '—')
  return (
    <Card class="flex flex-col">
      <div class="flex min-w-0 items-center gap-3.5"><FileText size={36} strokeWidth={1} class="shrink-0" /><div class="min-w-0"><h3 class="text-base leading-snug [overflow-wrap:anywhere]">{entry ? entry.file.path : t('No file selected')}</h3><small class="text-muted">{entry ? where(entry.storage) : t('Select a G-code file to view its details.')}</small></div></div>
      <div class="my-4 grid min-h-56 place-items-center overflow-hidden rounded-lg border border-edge">{thumb && <img src={thumb} alt="G-code model preview" class="max-h-72 w-full object-contain p-2.5" />}</div>
      <div>
        <Row label="Size" value={entry ? fileSize(entry.file.size) : '—'} />
        <Row label="Layers" value={meta && ok(meta.layers) && Number(meta.layers) > 0 ? Math.round(Number(meta.layers)) : '—'} />
        <Row label="Estimated Print Time" value={meta && ok(meta.estimated_seconds) ? duration(meta.estimated_seconds) : '—'} />
        <Row label="Filament (est.)" value={meta && ok(meta.filament_grams) ? `${Number(meta.filament_grams).toFixed(1)} g` : '—'} />
        <Row label="Nozzle Temperature" value={meta ? temp(meta.nozzle_temperature, 500) : '—'} />
        <Row label="Bed Temperature" value={meta ? temp(meta.bed_temperature, 200) : '—'} />
      </div>
      <Button wide variant="primary" class="mt-4" disabled={!entry} onClick={() => entry && onPrint(entry)}><Play {...I} />{t('Start Protected Print')}</Button>
      <Button wide variant="danger" class="mt-2" disabled={!entry} onClick={() => entry && onDelete(entry)}><Trash2 {...I} />{t('Delete')}</Button>
      <p class="mt-3 text-center text-muted">{t('Confirmation is required for protected actions.')}</p>
    </Card>
  )
}

const Upload = ({ busy, setBusy, refresh }: { busy: boolean; setBusy: (b: boolean) => void; refresh: () => Promise<void> }) => {
  const pick = useRef<HTMLInputElement>(null)
  const [storage, setStorage] = useState('internal'), [pct, setPct] = useState(-1), [note, setNote] = useState('Maximum 64 MiB · no automatic printing · existing files are never overwritten')
  const go = async () => {
    const file = pick.current?.files?.[0]
    if (busy || !file) return notify(t('Choose a G-code file first.'))
    if (!file.name || new TextEncoder().encode(file.name).length >= 256 || !/\.gcode$/i.test(file.name) || file.size === 0 || file.size > 64 * 1024 * 1024) return notify(t('Choose a valid .gcode file between 1 byte and 64 MiB.'))
    if (!(await ask(`${tpl('Upload {name} ({size}) to {where}?', { name: file.name, size: fileSize(file.size), where: where(storage) })}\n\n${t('The file will not be printed automatically.')}`))) return
    setBusy(true); setPct(0); setNote(tpl('Uploading… {n}%', { n: 0 }))
    try {
      await new Promise<void>((resolve, reject) => {
        const x = new XMLHttpRequest()
        x.open('POST', `/api/gcode-files/upload?storage=${encodeURIComponent(storage)}&name=${encodeURIComponent(file.name)}`)
        x.setRequestHeader('Content-Type', 'application/octet-stream'); x.timeout = 180000
        x.upload.onprogress = ev => { if (ev.lengthComputable) { const p = Math.min(100, Math.round(ev.loaded * 100 / ev.total)); setPct(p); setNote(tpl('Uploading… {n}%', { n: p })) } }
        x.onerror = () => reject(Error(t('Network error')))
        x.ontimeout = () => reject(Error(t('Upload timed out')))
        x.onload = () => { if (x.status === 201) resolve(); else { let m = `HTTP ${x.status}`; try { m = JSON.parse(x.responseText).error || m } catch { /* keep HTTP status */ } reject(Error(m)) } }
        x.send(file)
      })
      pick.current!.value = ''; setPct(100); setNote(t('Upload completed. File is ready in the selected storage.'))
      notify(tpl('{name} uploaded to {where}.', { name: file.name, where: where(storage) }))
      await refresh()
    } catch (e) { const m = tpl('Upload failed: {error}', { error: errText(e) }); setNote(m); notify(m) } finally { setBusy(false) }
  }
  return (
    <Card><CardHead title="Upload File" end={t('Local upload')} />
      <div class="grid grid-cols-[minmax(0,1fr)] justify-items-center gap-2.5 rounded-lg border border-dashed border-cyan p-3">
        <input ref={pick} type="file" accept=".gcode" class="w-full min-w-0" disabled={busy} aria-label="Select G-code file" />
        <label class="flex items-center gap-2">{t('Destination')} <Select class="w-auto" value={storage} disabled={busy} onChange={e => setStorage(e.currentTarget.value)}><option value="internal">{t('Internal memory')}</option><option value="usb">{t('USB drive')}</option></Select></label>
        <Button variant="primary" disabled={busy} onClick={go}><UploadIcon {...I} />{t('Upload G-code')}</Button>
        {pct >= 0 && <progress class="w-full" value={pct} max="100" />}
        <small class="text-center text-muted">{t(note)}</small>
      </div>
    </Card>
  )
}
