import type { ComponentChildren } from 'preact'
import { useEffect, useRef } from 'preact/hooks'

const FOCUSABLE =
  'a[href],button:not([disabled]),input:not([disabled]),select:not([disabled]),textarea:not([disabled]),[tabindex]:not([tabindex="-1"])'

// Every dialog title: same size and weight; callers set the margin that fits what follows (subtitle, eyebrow, content).
export const DialogTitle = ({ children, class: c = '' }: { children: ComponentChildren; class?: string }) => (
  <h2 class={`text-xl font-semibold ${c}`}>{children}</h2>
)

// Modal shell: Escape / backdrop click close it unless `locked`; focus moves in, stays in (Tab is trapped) and returns on close.
export const Dialog = ({
  onClose,
  locked,
  closeOnBackdrop = true,
  children,
  width = 520,
  class: className = '',
  label,
  sheet,
}: {
  onClose?: () => void
  locked?: boolean
  closeOnBackdrop?: boolean
  children: ComponentChildren
  width?: number
  class?: string
  label?: string
  sheet?: boolean
}) => {
  const box = useRef<HTMLDivElement>(null),
    props = useRef({ onClose, locked })
  props.current = { onClose, locked } // read at event time so the effect below runs only on mount/unmount
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    const items = () => [...(box.current?.querySelectorAll<HTMLElement>(FOCUSABLE) || [])]
    ;(box.current?.querySelector<HTMLElement>('[autofocus]') || items()[0])?.focus()
    const key = (e: KeyboardEvent) => {
      const open = document.querySelectorAll('[role=dialog]')
      if (open[open.length - 1] !== box.current) return // only the topmost dialog reacts
      if (e.key === 'Escape' && !props.current.locked && props.current.onClose) return props.current.onClose()
      if (e.key !== 'Tab') return
      const list = items()
      if (!list.length) return
      const first = list[0],
        last = list[list.length - 1]
      if (e.shiftKey && document.activeElement === first) {
        e.preventDefault()
        last.focus()
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault()
        first.focus()
      }
    }
    addEventListener('keydown', key)
    return () => {
      removeEventListener('keydown', key)
      previous?.focus?.()
    }
  }, [])
  // The backdrop's one column is the room inside its padding (beside a desktop scrollbar too), and a dialog takes
  // at most all of it, so the margin is the same on both sides. A width in vw would not leave it: 96vw is wider
  // than that room below 800 px, and the dialog ran into the right edge of a phone screen.
  return (
    // biome-ignore lint/a11y/noStaticElementInteractions: a click on the backdrop is a pointer shortcut; Escape is the keyboard path
    <div
      role="presentation"
      class={`fixed inset-0 z-50 grid grid-cols-1 place-items-center bg-black/60 p-4 [backdrop-filter:blur(6px)]${sheet ? ' cc2-dialog-sheet' : ''}`}
      onClick={e => e.target === e.currentTarget && closeOnBackdrop && !locked && onClose?.()}
    >
      <div
        ref={box}
        class={`cc2-dialog max-h-[92vh] overflow-auto rounded-xl border border-edge bg-panel p-6 shadow-pop ${className}`}
        style={{ width: `min(${width}px,100%)` }}
        role="dialog"
        aria-modal="true"
        aria-label={label}
      >
        {children}
      </div>
    </div>
  )
}
