import { store } from './store'
import { t, tpl } from './i18n'
import { ask } from './confirm'

export const request = async (path: string, options: RequestInit = {}) => {
  const controller = new AbortController()
  const timeout = window.setTimeout(() => controller.abort(), 8000)
  try {
    const headers = new Headers(options.headers)
    if (options.method && !['GET', 'HEAD'].includes(options.method.toUpperCase())) headers.set('X-CC2-Request', '1')
    const response = await fetch(path, {
      cache: 'no-store',
      ...options,
      headers,
      signal: options.signal ?? controller.signal,
    })
    const type = response.headers.get('content-type') || ''
    const data = type.includes('json') ? await response.json() : await response.text()
    if (!response.ok) throw Error(data?.error || data || `HTTP ${response.status}`)
    return data
  } finally {
    clearTimeout(timeout)
  }
}
export const post = (path: string, body = '') =>
  request(path, { method: 'POST', headers: { 'Content-Type': 'text/plain;charset=UTF-8' }, body })

export const toast = store({ text: '', n: 0, tone: 'info' as 'info' | 'error' })
export const notify = (text: string, tone: 'info' | 'error' = 'info') => toast.set(s => ({ text, tone, n: s.n + 1 }))

// Sends a protected control action; `confirmation` is an already translated question.
export async function control(action: string, confirmation = '') {
  if (confirmation && !(await ask(confirmation, /cancel|emergency|exclude/.test(action)))) return false
  try {
    await post('/api/control', action)
    notify(t('common.command_accepted'))
    return true
  } catch (e: any) {
    notify(tpl('common.rejected_error', { error: e.message }), 'error')
    return false
  }
}

export const errText = (e: any) => String(e?.message || e)
export { t, tpl }
