import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '@/lib/utils'

const button = cva(
  'inline-flex items-center justify-center gap-2 rounded-lg border text-sm font-medium min-h-9 px-3 py-1.5 transition-[color,background-color,border-color,box-shadow] duration-150 active:enabled:translate-y-px disabled:opacity-50 disabled:cursor-not-allowed cursor-pointer',
  {
    variants: {
      variant: {
        default: 'border-edge bg-field hover:enabled:border-cyan hover:enabled:bg-field/70',
        primary: 'border-cyan bg-cyan text-ink font-semibold shadow-glow hover:enabled:brightness-110',
        danger: 'border-red/70 text-red bg-red/10 hover:enabled:bg-red/20',
        active: 'border-cyan text-cyan bg-cyan/10',
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
