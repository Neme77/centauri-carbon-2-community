import { request } from './api'
import { t } from './i18n'

// One G-code file of a storage as /api/gcode-files lists it: `path`, `size`, `modified`.
export type FileEntry = { storage: 'internal' | 'usb'; file: any }
// A storage whose list is partial (traversal guards, or an older backend): `n` of `total` files are shown.
export type FileCut = { storage: string; n: number; total: number }

// What tells this file from another one of the same name, for a cached picture.
export const fileTag = (file: any) => `${Number(file?.size || 0)}:${Number(file?.modified || 0)}`

// Every G-code file of both storages: the first reply of /api/gcode-files and the pages that follow it.
export async function listFiles(signal?: AbortSignal) {
  const data = await request('/api/gcode-files', { signal })
  const entries: FileEntry[] = []
  const cut: FileCut[] = []
  for (const [storage, group] of [
    ['internal', data.internal],
    ['usb', data.usb],
  ] as const) {
    const files: any[] = [...(group?.files || [])]
    let batch = group
    let scanCut = Boolean(group?.scan_truncated)
    const cursors = new Set<string>()
    while (batch?.has_more) {
      const cursor = batch.next
      if (!cursor || typeof cursor.modified !== 'string' || typeof cursor.path !== 'string' || cursors.size >= 32)
        throw Error(t('files.list_changed_retry'))
      const encoded = new URLSearchParams({ storage, before: cursor.modified, file: cursor.path }).toString()
      if (cursors.has(encoded)) throw Error(t('files.list_changed_retry'))
      cursors.add(encoded)
      batch = (await request(`/api/gcode-files/page?${encoded}`, { signal }))[storage]
      if (!batch || !Array.isArray(batch.files)) throw Error(t('files.list_changed_retry'))
      files.push(...batch.files)
      scanCut ||= Boolean(batch.scan_truncated)
    }
    const unique = new Map(files.map(file => [file.path, file]))
    for (const file of unique.values()) entries.push({ storage, file })
    if (scanCut || (group?.has_more === undefined && group?.truncated))
      cut.push({ storage, n: unique.size, total: Math.max(Number(group.total) || 0, unique.size) })
  }
  return { entries, cut, space: { internal: data.internal?.space, usb: data.usb?.space } }
}
