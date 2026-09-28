import type { ComponentChildren } from 'preact'
import { useEffect } from 'preact/hooks'

// Modal shell: backdrop click and Escape close it unless `locked`.
export const Dialog = ({ onClose, locked, children, width = 520 }: { onClose?: () => void; locked?: boolean; children: ComponentChildren; width?: number }) => {
  useEffect(() => {
    if (locked || !onClose) return
    const k = (e: KeyboardEvent) => e.key === 'Escape' && onClose()
    addEventListener('keydown', k)
    return () => removeEventListener('keydown', k)
  }, [locked, onClose])
  return (
    <div class="fixed inset-0 z-50 grid place-items-center bg-black/80 p-4" onClick={e => e.target === e.currentTarget && !locked && onClose?.()}>
      <div class="max-h-[92vh] overflow-auto rounded-xl border border-edge bg-panel p-6 shadow-2xl" style={{ width: `min(${width}px,96vw)` }} role="dialog" aria-modal="true">{children}</div>
    </div>
  )
}
