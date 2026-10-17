import { request } from './api'
import { t } from './i18n'
import { store, ls } from './store'

export type Release = {
  version: string
  firmware: string
  channel: string
  url: string
  size: number
  notes: string
  notes_i18n?: Record<string, string>
}

export const releaseNotes = (release: Release, language: string) => {
  for (const code of [language, 'en']) {
    const text = release.notes_i18n?.[code]
    if (typeof text === 'string' && text.trim() && text.length <= 2048) return { text, language: code }
  }
  return { text: release.notes, language: 'en' }
}
export type Catalog = { schema: number; expires: number; releases: Release[] }
export type Status = {
  enabled: boolean
  ready: boolean
  busy: boolean
  installed: string
  firmware: string
  job: string
  phase: string
  version: string
}
export const status = store<{ data: Status | null }>({ data: null })
let statusPending: Promise<Status> | null = null
export function refresh() {
  if (!statusPending)
    statusPending = request('/api/app-update/status')
      .then(data => {
        status.set({ data })
        return data as Status
      })
      .finally(() => {
        statusPending = null
      })
  return statusPending
}
const source =
  'https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/develop/cc2-control/update-channel.cc2catalog'
const urlPattern =
  /^https:\/\/github\.com\/Neme77\/centauri-carbon-2-community\/releases\/download\/[A-Za-z0-9._-]+\/[A-Za-z0-9._-]+\.cc2update$/
const validVersion = /^\d{1,6}\.\d{1,6}\.\d{1,6}$/
export const newer = (candidate: string, installed: string) => {
  if (!validVersion.test(candidate) || !validVersion.test(installed)) return false
  const a = candidate.split('.').map(Number),
    b = installed.split('.').map(Number)
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] > b[i]
  return false
}
export const compatible = (index: Catalog | null, firmware: string, channel: string): Release[] => {
  if (
    index?.schema !== 1 ||
    !Number.isSafeInteger(index.expires) ||
    index.expires <= Date.now() / 1000 ||
    !Array.isArray(index.releases)
  )
    return []
  return index.releases
    .filter(
      r =>
        r.firmware === firmware &&
        r.channel === channel &&
        validVersion.test(r.version) &&
        urlPattern.test(r.url) &&
        Number.isSafeInteger(r.size) &&
        r.size >= 1108 &&
        r.size <= 33555540 &&
        typeof r.notes === 'string' &&
        r.notes.length <= 2048
    )
    .sort((a, b) => (newer(a.version, b.version) ? -1 : newer(b.version, a.version) ? 1 : 0))
}

async function checkIndex() {
  const response = await fetch(source, {
    cache: 'no-store',
    signal: AbortSignal.timeout(10000),
    credentials: 'omit',
    referrerPolicy: 'no-referrer',
  })
  if (response.status === 404) return null
  if (!response.ok || !response.body) throw Error(t('updates.check_failed'))
  const reader = response.body.getReader(),
    chunks: Uint8Array[] = []
  let total = 0
  try {
    for (;;) {
      const { value, done } = await reader.read()
      if (done) break
      total += value.length
      if (total > 8264) throw Error(t('updates.check_failed'))
      chunks.push(value)
    }
  } finally {
    await reader.cancel()
  }
  return request('/api/app-update/catalog', {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: new Blob(chunks as BlobPart[]),
  })
}

export const updateCatalog = store({ index: null as Catalog | null, checking: false })
export const updateChannel = store({ value: ls.get('cc2-update-channel') === 'canvas-uds' ? 'canvas-uds' : 'stable' })
export const setUpdateChannel = (value: string) => {
  const next = value === 'canvas-uds' ? value : 'stable'
  ls.set('cc2-update-channel', next)
  updateChannel.set({ value: next })
}
addEventListener('storage', event => {
  if (event.key === 'cc2-update-channel')
    updateChannel.set({ value: event.newValue === 'canvas-uds' ? 'canvas-uds' : 'stable' })
})

export const UPDATE_CHECK_INTERVAL = 6 * 3600000
export const UPDATE_RETRY_INTERVAL = 15 * 60000
let checkPending: Promise<Catalog | null> | null = null
let nextAutomatic = 0

// Both the header and Settings use one verification path and one in-flight check.
// Only a server-verified catalog can reach the notification; no package endpoint is called here.
export function checkUpdates(force = false): Promise<Catalog | null> {
  if (checkPending) return checkPending
  updateCatalog.set({ checking: true })
  checkPending = (async () => {
    const current = await refresh()
    if (!current.enabled) {
      updateCatalog.set({ index: null })
      nextAutomatic = Date.now() + UPDATE_RETRY_INTERVAL
      return null
    }
    if (current.busy) {
      nextAutomatic = Date.now() + UPDATE_RETRY_INTERVAL
      return updateCatalog.get().index
    }
    const cached = await request('/api/app-update/catalog')
    const age = Date.now() - Number(ls.get('cc2-update-checked') || 0)
    const useCache = !force && age >= 0 && age < UPDATE_CHECK_INTERVAL && cached?.expires > Date.now() / 1000
    const result: Catalog | null = useCache ? cached : await checkIndex()
    updateCatalog.set({ index: result })
    if (result && !useCache) ls.set('cc2-update-checked', String(Date.now()))
    nextAutomatic =
      Date.now() + (useCache ? UPDATE_CHECK_INTERVAL - age : result ? UPDATE_CHECK_INTERVAL : UPDATE_RETRY_INTERVAL)
    return result
  })()
    .catch(error => {
      nextAutomatic = Date.now() + UPDATE_RETRY_INTERVAL
      throw error
    })
    .finally(() => {
      checkPending = null
      updateCatalog.set({ checking: false })
    })
  return checkPending
}

// Registered with the shared scheduler: hidden tabs sleep and reopening does not bypass the backoff.
export async function checkUpdateNotice() {
  if (Date.now() < nextAutomatic) return
  try {
    await checkUpdates()
  } catch {
    /* Silent automatic check; manual Settings checks report errors. */
  }
}
