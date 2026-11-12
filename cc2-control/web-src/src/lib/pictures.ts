import { useLayoutEffect, useState } from 'preact/hooks'

// The picture a slicer embedded in a G-code file, as an object URL ('' when the file has none). The printer
// reads up to 4 MB of a file to find it, so a picture is asked for once and the requests go one at a time; one
// that nothing shows any more when its turn comes (the list went to another page) is dropped. Only an answer is
// remembered: a request that failed is made again when the picture is next shown.
//
// The answers are kept for rows that come back (another page of a list, another list), but not without bound:
// beyond KEEP pictures that nothing shows, the least recently shown go, and their object URLs are revoked. A
// picture of a file that has changed since (another `file:` tag for the same file) goes as soon as nothing
// shows it. A URL is never revoked while a mounted picture uses it.
export const KEEP = 48

type Picture = {
  storage: string
  path: string
  tag: string
  url: string | null // null: not answered yet
  users: number // mounted pictures that show it
  stale: boolean // the file has changed since: dropped as soon as nothing shows it
  used: number // when it was last shown or answered, to drop the least recently used first
  listeners: Set<(url: string) => void>
}
const pictures = new Map<string, Picture>()
const waiting: string[] = []
let loading = false
let clock = 0
let trimming = false

const drop = (key: string, picture: Picture) => {
  pictures.delete(key)
  if (picture.url) URL.revokeObjectURL(picture.url)
}

// Runs after the commit that unmounted a picture, so that a row of the same file mounted by the same commit
// has already taken it.
const trim = () => {
  if (trimming) return
  trimming = true
  queueMicrotask(() => {
    trimming = false
    const idle = [...pictures].filter(([, p]) => !p.users && p.url !== null)
    for (const [key, p] of idle) if (p.stale) drop(key, p)
    const kept = idle.filter(([key]) => pictures.has(key)).sort((a, b) => a[1].used - b[1].used)
    for (const [key, p] of kept.slice(0, Math.max(0, kept.length - KEEP))) drop(key, p)
  })
}

const load = async () => {
  if (loading) return
  loading = true
  for (let key = waiting.shift(); key !== undefined; key = waiting.shift()) {
    const picture = pictures.get(key)
    if (!picture || picture.url !== null) continue
    if (!picture.users) {
      pictures.delete(key)
      continue
    }
    let url = ''
    let answered = false
    try {
      const r = await fetch('/api/gcode-files/thumbnail', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain;charset=UTF-8', 'X-CC2-Request': '1' },
        body: `${picture.storage}\n${picture.path}`,
      })
      const blob = await r.blob()
      if (r.ok && blob.type.startsWith('image/')) url = URL.createObjectURL(blob)
      answered = r.ok || r.status === 404
    } catch {
      /* asked again when the picture is next shown */
    }
    if (answered && pictures.get(key) === picture) {
      picture.url = url
      picture.used = ++clock
    } else {
      if (pictures.get(key) === picture) pictures.delete(key)
      if (url) URL.revokeObjectURL(url)
      url = ''
    }
    for (const tell of picture.listeners) tell(url)
    trim()
  }
  loading = false
}

// `tag` tells two files of one name apart: `file:<size>:<modified>` for a file of a list (a newer tag of the
// same file makes the older picture stale), or another kind (the queue's `job:<id>`) that only stays apart.
// An empty `path` asks for nothing: the file is not there.
export const useFilePicture = (storage: string, path: string, tag = ''): string => {
  const key = `${storage}\n${path}\n${tag}`
  const [url, setUrl] = useState(() => (path && pictures.get(key)?.url) || '')
  // A layout effect: the picture is taken in the same commit that renders it, before trim() can drop it.
  useLayoutEffect(() => {
    if (!path) {
      setUrl('')
      return
    }
    let picture = pictures.get(key)
    if (!picture) {
      picture = { storage, path, tag, url: null, users: 0, stale: false, used: 0, listeners: new Set() }
      if (tag.startsWith('file:'))
        for (const other of pictures.values())
          if (other.storage === storage && other.path === path && other.tag !== tag && other.tag.startsWith('file:'))
            other.stale = true
      pictures.set(key, picture)
      waiting.push(key)
    }
    const shown = picture
    shown.users++
    shown.used = ++clock
    shown.listeners.add(setUrl)
    setUrl(shown.url || '')
    if (shown.url === null) void load()
    return () => {
      shown.users--
      shown.listeners.delete(setUrl)
      if (!shown.users) trim()
    }
  }, [key])
  return url
}
