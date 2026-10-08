import {
  ArrowUpDown,
  Box,
  Camera,
  Crosshair,
  ExternalLink,
  Fan,
  File,
  Folder,
  Grid3x3,
  House,
  Info,
  Layers,
  Lightbulb,
  Link,
  Lock,
  LockOpen,
  MessageSquare,
  Monitor,
  Palette,
  Plug,
  Power,
  Settings,
  ShieldCheck,
  SlidersHorizontal,
  Spool,
  SquareTerminal,
  Thermometer,
  Zap,
} from 'lucide-preact'
import { CanvasIcon } from '@/components/canvas-icon'

const icons = {
  home: House,
  control: SlidersHorizontal,
  file: File,
  folder: Folder,
  grid: Grid3x3,
  layers: Layers,
  target: Crosshair,
  console: SquareTerminal,
  settings: Settings,
  camera: Camera,
  temp: Thermometer,
  fan: Fan,
  bolt: Zap,
  lock: Lock,
  unlock: LockOpen,
  z: ArrowUpDown,
  link: Link,
  shield: ShieldCheck,
  plug: Plug,
  palette: Palette,
  monitor: Monitor,
  message: MessageSquare,
  cube: Box,
  open: ExternalLink,
  light: Lightbulb,
  info: Info,
  motors: Power,
  spool: Spool,
}

// Lucide icons (plus the Canvas glyph, drawn the same way), with a 1 px stroke everywhere.
export const Icon = ({ n, class: c = '' }: { n: string; class?: string }) => {
  if (n === 'canvas') return <CanvasIcon strokeWidth={1} class={`size-5 shrink-0 ${c}`} />
  const Glyph = icons[n as keyof typeof icons]
  return <Glyph strokeWidth={1} aria-hidden="true" class={`size-5 shrink-0 ${c}`} />
}
