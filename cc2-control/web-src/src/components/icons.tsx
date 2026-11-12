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
  History,
  Info,
  Layers,
  Lightbulb,
  Link,
  ListOrdered,
  Lock,
  LockOpen,
  MessageSquare,
  Monitor,
  Palette,
  Pause,
  Plug,
  Power,
  Settings,
  ShieldCheck,
  SlidersHorizontal,
  Spool,
  SquareTerminal,
  Thermometer,
  Zap,
  Wifi,
  EthernetPort,
} from 'lucide-preact'
import { CanvasIcon } from '@/components/canvas-icon'

const icons = {
  wifi: Wifi,
  ethernet: EthernetPort,
  home: House,
  history: History,
  control: SlidersHorizontal,
  file: File,
  folder: Folder,
  grid: Grid3x3,
  layers: Layers,
  target: Crosshair,
  console: SquareTerminal,
  settings: Settings,
  camera: Camera,
  pause: Pause,
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
  queue: ListOrdered,
}

// Lucide icons (plus the Canvas glyph, drawn the same way), with a 1.75 px stroke everywhere.
export const Icon = ({
  n,
  class: c = '',
  style,
}: {
  n: string
  class?: string
  style?: Record<string, string | number>
}) => {
  if (n === 'canvas') return <CanvasIcon strokeWidth={1.75} class={`size-5 shrink-0 ${c}`} />
  const Glyph = icons[n as keyof typeof icons]
  return <Glyph strokeWidth={1.75} aria-hidden="true" class={`size-5 shrink-0 ${c}`} style={style} />
}
