// Header title framed by the door-inspired artwork (cadre2.svg): a technical line with a dot at each end, 45° chamfers
// and a hatched pocket on the left, then a continuous line running under the title. From xl up only; below that the
// title stands alone. Scale: the artwork's 268 units of height become the 56 px of the header block (0.209 px per unit).
export const HeaderTitle = () => (
  <div class="cc2-header-title flex min-w-0 flex-1 items-stretch xl:h-14">
    <svg aria-hidden="true" viewBox="0 16 702 268" class="hidden h-full w-auto shrink-0 text-cyan opacity-80 xl:block">
      <defs>
        <pattern
          id="cc2-hachures"
          width="107.65"
          height="200"
          patternUnits="userSpaceOnUse"
          patternTransform="translate(5.3 0) skewX(-45)"
        >
          <rect width="50" height="200" fill="currentColor" />
        </pattern>
        <clipPath id="cc2-zone">
          <path d="M156.6 64 H444.5 L631.43 250.93 A10 10 0 0 1 624.36 268 H56 V164.6 Z" />
        </clipPath>
      </defs>
      <polygon
        points="46,36 10,72 10,36"
        fill="currentColor"
        transform="matrix(1.3888889,0,0,1.3888889,8.109735,-19.996551)"
      />
      <rect x="0" y="16" width="702" height="268" fill="url(#cc2-hachures)" clip-path="url(#cc2-zone)" />
      <path
        d="M16 263.68732 V148 L140 24 h321.1 l240 240"
        fill="none"
        stroke="currentColor"
        stroke-width="8"
        stroke-linejoin="miter"
      />
      <circle cx="16" cy="254" r="14" fill="currentColor" />
    </svg>
    <span class="cc2-portrait-brand hidden font-bold text-cyan">CC2 Control</span>
    <div class="cc2-full-brand relative min-w-0 flex-1">
      <h1 class="text-2xl font-extrabold leading-tight tracking-tight text-cyan [text-shadow:0_0_14px_color-mix(in_srgb,currentColor_35%,transparent)] md:text-[28px] xl:absolute xl:bottom-[7px] xl:left-3 xl:whitespace-nowrap xl:text-[32px]">
        Centauri Carbon 2<span class="mx-2.5 font-light opacity-60">/</span>
        <span class="text-[0.8em] font-semibold">Control Center</span>
      </h1>
      {/* The line continues at the artwork's baseline (y = 263.7 units) and ends with a dot before the badges. */}
      <svg
        aria-hidden="true"
        class="absolute inset-x-0 top-[50.3px] hidden h-[3px] w-full overflow-visible text-cyan opacity-80 xl:block"
        fill="none"
        stroke="currentColor"
      >
        <line x1="0" y1="1.5" x2="100%" y2="1.5" stroke-width="1.67" />
        <circle cx="100%" cy="1.5" r="2.93" fill="currentColor" stroke="none" />
      </svg>
    </div>
  </div>
)
