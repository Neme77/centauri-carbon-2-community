import { ArrowUpDown, Box, Camera, Crosshair, ExternalLink, Fan, File, Folder, Grid3x3, House, Info, Lightbulb, Link, Lock, LockOpen, MessageSquare, Monitor, Palette, Plug, Power, Settings, ShieldCheck, SlidersHorizontal, SquareTerminal, Thermometer, Zap } from 'lucide-preact'

const icons = {
  home: House, control: SlidersHorizontal, file: File, folder: Folder, grid: Grid3x3, target: Crosshair, console: SquareTerminal,
  settings: Settings, camera: Camera, temp: Thermometer, fan: Fan, bolt: Zap, lock: Lock, unlock: LockOpen, z: ArrowUpDown,
  link: Link, shield: ShieldCheck, plug: Plug, palette: Palette, monitor: Monitor, message: MessageSquare, cube: Box, open: ExternalLink, light: Lightbulb, info: Info, motors: Power,
}

// Lucide icons, drawn with a 1 px stroke everywhere.
export const Icon = ({ n, class: c = '' }: { n: string; class?: string }) => {
  const Glyph = icons[n as keyof typeof icons]
  return <Glyph strokeWidth={1} aria-hidden="true" class={`size-5 shrink-0 ${c}`} />
}
