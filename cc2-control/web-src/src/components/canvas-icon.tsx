// ELEGOO Canvas glyph: four filament spools, redrawn from canvas.svg following Lucide's drawing rules:
// 24 x 24 grid, path centrelines inside a 2 px margin (2..22), clear space between shapes, round strokes,
// a stroked r=1 circle for the hub (as in Lucide's circle-dot), and the same size / strokeWidth / colour
// props as the Lucide components.
const CENTERS = [6.25, 17.75]

export const CanvasIcon = ({
  size = 24,
  strokeWidth = 1,
  class: c = '',
}: {
  size?: string | number
  strokeWidth?: string | number
  class?: string
}) => (
  <svg
    viewBox="0 0 24 24"
    width={size}
    height={size}
    fill="none"
    stroke="currentColor"
    stroke-width={strokeWidth}
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
    class={c}
  >
    {CENTERS.flatMap(y =>
      CENTERS.map(x => (
        <g key={`${x}-${y}`}>
          <circle cx={x} cy={y} r="4.25" />
          <circle cx={x} cy={y} r="1" />
        </g>
      ))
    )}
  </svg>
)
