import type { ComponentChildren } from 'preact'
import { useEffect, useRef } from 'preact/hooks'

const FOCUSABLE = 'a[href],button:not([disabled]),input:not([disabled]),select:not([disabled]),textarea:not([disabled]),[tabindex]:not([tabindex="-1"])'

// Modal shell: Escape / backdrop click close it unless `locked`; focus moves in, stays in (Tab is trapped) and returns on close.
export const Dialog = ({ onClose, locked, children, width = 520 }: { onClose?: () => void; locked?: boolean; children: ComponentChildren; width?: number }) => {
  const box = useRef<HTMLDivElement>(null), props = useRef({ onClose, locked })
  props.current = { onClose, locked } // read at event time so the effect below runs only on mount/unmount
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    const items = () => [...(box.current?.querySelectorAll<HTMLElement>(FOCUSABLE) || [])]
    ;(box.current?.querySelector<HTMLElement>('[autofocus]') || items()[0])?.focus()
    const key = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !props.current.locked && props.current.onClose) return props.current.onClose()
      if (e.key !== 'Tab') return
      const list = items()
      if (!list.length) return
      const first = list[0], last = list[list.length - 1]
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus() }
      else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus() }
    }
    addEventListener('keydown', key)
    return () => { removeEventListener('keydown', key); previous?.focus?.() }
  }, [])
  return (
    <div class="fixed inset-0 z-50 grid place-items-center bg-black/80 p-4" onClick={e => e.target === e.currentTarget && !locked && onClose?.()}>
      <div ref={box} class="max-h-[92vh] overflow-auto rounded-xl border border-edge bg-panel p-6 shadow-2xl" style={{ width: `min(${width}px,96vw)` }} role="dialog" aria-modal="true">{children}</div>
    </div>
  )
}
