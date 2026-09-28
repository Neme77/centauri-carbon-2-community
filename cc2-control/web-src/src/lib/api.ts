import { store } from './store'
import { t, tpl } from './i18n'
import { ask } from './confirm'

export const request = async (path: string, options: RequestInit = {}) => {
  const response = await fetch(path, { cache: 'no-store', ...options })
  const type = response.headers.get('content-type') || ''
  const data = type.includes('json') ? await response.json() : await response.text()
  if (!response.ok) throw Error((data && data.error) || data || `HTTP ${response.status}`)
  return data
}
export const post = (path: string, body = '') =>
  request(path, { method: 'POST', headers: { 'Content-Type': 'text/plain;charset=UTF-8' }, body })

export const toast = store({ text: '', n: 0 })
export const notify = (text: string) => toast.set(s => ({ text, n: s.n + 1 }))

// Sends a protected control action; `confirmation` is an already translated question.
export async function control(action: string, confirmation = '') {
  if (confirmation && !(await ask(confirmation, /cancel|emergency|exclude/.test(action)))) return false
  try {
    await post('/api/control', action)
    notify(tpl('Accepted: {action}', { action }))
    return true
  } catch (e: any) {
    notify(tpl('Rejected: {error}', { error: e.message }))
    return false
  }
}

export const errText = (e: any) => String(e && e.message || e)
export { t, tpl }
