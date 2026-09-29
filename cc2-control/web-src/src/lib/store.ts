import { useEffect, useState } from 'preact/hooks'

// Minimal external store: get / set (shallow merge) / use (hook that re-renders on change).
export function store<T extends object>(init: T) {
  let v = init, ver = 0
  const subs = new Set<() => void>()
  return {
    get: () => v,
    set(p: Partial<T> | ((s: T) => Partial<T>)) {
      v = { ...v, ...(typeof p === 'function' ? p(v) : p) }
      ver++
      for (const f of subs) f()
    },
    use() {
      const [, bump] = useState(0), seen = ver
      useEffect(() => {
        const f = () => bump(n => n + 1)
        subs.add(f)
        if (seen !== ver) f() // a set() landed between render and subscription
        return () => { subs.delete(f) }
      }, [])
      return v
    },
  }
}

export const ls = {
  get: (k: string) => { try { return localStorage.getItem(k) } catch { return null } },
  set: (k: string, v: string) => { try { localStorage.setItem(k, v) } catch { /* private mode */ } },
  del: (k: string) => { try { localStorage.removeItem(k) } catch { /* private mode */ } },
}
