import { useEffect, useState } from 'preact/hooks'
import { request } from './api'
import { store } from './store'

// localStorage is scoped to this exact origin (scheme, printer address and port).
// Never include this key in preferences, diagnostics or exported backups.
const key = 'cc2-remembered-lan-code'
const valid = (value: string | null): value is string => Boolean(value && /^[A-Za-z0-9._-]{1,127}$/.test(value))
const read = () => {
  try {
    const value = localStorage.getItem(key)
    return valid(value) ? value : ''
  } catch {
    return ''
  }
}
export const rememberedLan = store({ code: read() })
let revision = 0
addEventListener('storage', event => {
  if (event.storageArea === localStorage && (event.key === key || event.key === null)) {
    revision++
    rememberedLan.set({ code: read() })
  }
})
export function forgetLanCode() {
  revision++
  try {
    localStorage.removeItem(key)
  } catch {
    // Still clear this page's in-memory copy if storage is unavailable.
  }
  rememberedLan.set({ code: '' })
}
export function rememberLanCode(code: string) {
  if (!valid(code)) return false
  try {
    localStorage.setItem(key, code)
    if (localStorage.getItem(key) !== code) return false
  } catch {
    return false
  }
  revision++
  rememberedLan.set({ code })
  return true
}
export function useLanCode() {
  const saved = rememberedLan.use().code
  const [code, setCode] = useState(saved)
  useEffect(() => setCode(saved), [saved])
  return [code, setCode, () => setCode(rememberedLan.get().code)] as const
}
// Credentials are sent only to an explicit protected endpoint, never to polling
// or catalog requests. Keep backend authorization and each action's confirmation.
export async function lanRequest(path: string, code: string, options: RequestInit = {}) {
  if (
    !/^\/api\/(access\/verify|experiments|app-update\/(upload|download|install)|bed-drying\/(start|stop))$/.test(path)
  )
    throw new Error('Unsupported LAN-protected endpoint')
  const sent = code.trim(),
    generation = revision
  const headers = new Headers(options.headers)
  headers.set('X-CC2-LAN', sent)
  try {
    const result = await request(path, { ...options, method: 'POST', headers })
    if (generation === revision && rememberedLan.get().code) rememberLanCode(sent)
    return result
  } catch (error) {
    if ((error as { status?: number }).status === 401 && rememberedLan.get().code === sent) forgetLanCode()
    throw error
  }
}
