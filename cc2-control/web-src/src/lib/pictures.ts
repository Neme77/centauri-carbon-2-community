import { useEffect, useState } from 'preact/hooks'

// The picture a slicer embedded in a G-code file, as an object URL ('' when the file has none). The printer
// reads up to 4 MB of a file to find it, so a picture is asked for once per page load and the requests go one
// at a time; one that nobody shows any more when its turn comes (the list went to another page) is dropped.
// Only an answer is remembered: a request that failed is made again when the picture is next shown.
type Picture = {
  storage: string
  path: string
  url: string | null // null: not asked yet
  shown: number
  listeners: Set<(url: string) => void>
}
const pictures = new Map<string, Picture>()
const waiting: string[] = []
let loading = false

const load = async () => {
  if (loading) return
  loading = true
  for (let key = waiting.shift(); key !== undefined; key = waiting.shift()) {
    const picture = pictures.get(key)
    if (!picture || picture.url !== null) continue
    if (!picture.shown) {
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
    if (answered) picture.url = url
    else pictures.delete(key)
    for (const tell of picture.listeners) tell(url)
  }
  loading = false
}

// `tag` tells two files of one name apart (the size and date of the file, or the queue job that prints it).
// An empty `path` asks for nothing: the file is not there.
export const useFilePicture = (storage: string, path: string, tag = ''): string => {
  const key = `${storage}\n${path}\n${tag}`
  const [url, setUrl] = useState(() => (path && pictures.get(key)?.url) || '')
  useEffect(() => {
    if (!path) {
      setUrl('')
      return
    }
    let picture = pictures.get(key)
    if (!picture) {
      picture = { storage, path, url: null, shown: 0, listeners: new Set() }
      pictures.set(key, picture)
      waiting.push(key)
    }
    setUrl(picture.url || '')
    if (picture.url !== null) return
    const asked = picture
    asked.shown++
    asked.listeners.add(setUrl)
    void load()
    return () => {
      asked.shown--
      asked.listeners.delete(setUrl)
    }
  }, [key])
  return url
}
