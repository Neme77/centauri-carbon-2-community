import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '@/lib/utils'

const button = cva(
  'inline-flex items-center justify-center gap-2 rounded-md border text-sm min-h-9 px-3 py-1.5 transition-colors disabled:opacity-50 disabled:cursor-not-allowed cursor-pointer',
  {
    variants: {
      variant: {
        default: 'border-edge bg-field hover:enabled:border-cyan',
        primary: 'border-cyan bg-cyan text-ink font-semibold',
        danger: 'border-red text-red bg-red/10',
        active: 'border-cyan text-cyan bg-field',
        ghost: 'border-transparent hover:enabled:bg-field',
      },
      wide: { true: 'w-full' },
    },
    defaultVariants: { variant: 'default' },
  }
)
type Props = preact.JSX.ButtonHTMLAttributes<HTMLButtonElement> & VariantProps<typeof button>
export const Button = ({ class: c, variant, wide, ...p }: Props) => (
  <button type="button" class={cn(button({ variant, wide }), c as string)} {...p} />
)
