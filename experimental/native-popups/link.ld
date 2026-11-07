/* SPDX-License-Identifier: GPL-3.0-or-later */
/* One RWX block appended to the screen program; mapped through a reused program header. */
ENTRY(nui_home_show)
SECTIONS {
  . = 0x01400000;
  .text : { *(.text.nui_home_show) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.note*) }
}
