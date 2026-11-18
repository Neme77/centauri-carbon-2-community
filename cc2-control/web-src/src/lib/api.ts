import { store } from './store'
import { t, tpl } from './i18n'
import { ask } from './confirm'
import { startTimeoutMs } from './start-timeout'

export const request = async (path: string, options: RequestInit & { timeoutMs?: number } = {}) => {
  const controller = new AbortController()
  const start = /\/api\/(gcode-files\/print|queue\/start)$/.test(path)
  const waitMs =
    options.timeoutMs ??
    (/\/api\/gcode-files\/(metadata|inspect|layer-path|object-preview)(\?|$)/.test(path)
      ? 120000
      : // A USB start answers only after the file has been copied to internal memory (#344), from the
        // print dialog and from the queue alike.
        start
        ? startTimeoutMs()
        : 8000)
  let timedOut = false
  const timeout = window.setTimeout(() => {
    timedOut = true
    controller.abort()
  }, waitMs)
  try {
    const headers = new Headers(options.headers)
    if (options.method && !['GET', 'HEAD'].includes(options.method.toUpperCase())) headers.set('X-CC2-Request', '1')
    const { timeoutMs: _timeout, ...rest } = options
    const response = await fetch(path, {
      cache: 'no-store',
      ...rest,
      headers,
      signal: options.signal ?? controller.signal,
    })
    const type = response.headers.get('content-type') || ''
    const data = type.includes('json') ? await response.json() : await response.text()
    // The reply stays on the error for callers that act on its fields (e.g. reboot_required).
    if (!response.ok)
      throw Object.assign(Error(data?.error || data || `HTTP ${response.status}`), { status: response.status, data })
    return data
  } catch (err) {
    // The page gave up waiting; closing the request also cancels the start on CC2 Control.
    if (start && timedOut) throw Error(tpl('print.start_no_answer', { minutes: String(Math.round(waitMs / 60000)) }))
    throw err
  } finally {
    clearTimeout(timeout)
  }
}
export const post = (path: string, body = '', timeoutMs?: number) =>
  request(path, { method: 'POST', headers: { 'Content-Type': 'text/plain;charset=UTF-8' }, body, timeoutMs })

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
