import { useEffect, useId, useRef, useState } from 'preact/hooks'
import { Icon } from '@/components/icons'

// A small (i) that explains something in one or two sentences. Opens on hover, on click or tap (a touch screen has
// no hover) and on Enter or Space from the keyboard; Escape, a click elsewhere or leaving the button closes it.
export const InfoHint = ({
  text,
  label = text,
  align = 'left',
  subtle = false,
  ...rest
}: {
  text: string
  label?: string
  // Which edge of the (i) the text box lines up with; 'right' for an (i) at the right edge of the page.
  align?: 'left' | 'right'
  // A smaller, muted symbol for a hint that sits in a table cell.
  subtle?: boolean
  'data-hint'?: string
}) => {
  const id = useId()
  const box = useRef<HTMLSpanElement>(null)
  const [pinned, setPinned] = useState(false)
  const [hover, setHover] = useState(false)
  const open = pinned || hover
  useEffect(() => {
    if (!pinned) return
    const away = (e: Event) => {
      if (!box.current?.contains(e.target as Node)) setPinned(false)
    }
    const onEscape = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setPinned(false)
    }
    document.addEventListener('pointerdown', away)
    document.addEventListener('keydown', onEscape)
    return () => {
      document.removeEventListener('pointerdown', away)
      document.removeEventListener('keydown', onEscape)
    }
  }, [pinned])
  return (
    <span ref={box} class="relative inline-flex" {...rest}>
      <button
        type="button"
        class={`inline-flex ${subtle ? 'size-5' : 'size-6'} cursor-help items-center justify-center rounded-full`}
        aria-label={label}
        aria-expanded={open}
        aria-describedby={open ? id : undefined}
        onClick={() => setPinned(!pinned)}
        onMouseEnter={() => setHover(true)}
        onMouseLeave={() => setHover(false)}
      >
        <Icon n="info" class={subtle ? 'size-3.5 text-muted' : 'size-4 text-cyan'} />
      </button>
      {open && (
        <span
          id={id}
          role="tooltip"
          class={`absolute top-full ${align === 'right' ? 'right-0' : 'left-0'} z-30 mt-1 w-64 max-w-[calc(100vw-2rem)] rounded-md border border-edge bg-panel p-2 text-left text-xs font-normal whitespace-pre-line text-fg shadow-pop`}
          data-hint-text
        >
          {text}
        </span>
      )}
    </span>
  )
}
