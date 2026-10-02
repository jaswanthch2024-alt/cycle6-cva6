/* link.ld -- CoreMark for VP + CVA6 co-sim.
 * Boot entry at 0x80000000 (co-sim bootrom jumps there). Everything else
 * starts at 0x80002000 so nothing occupies 0x80001000 (VP HTIF tohost).
 * Stack top is 0x8000F000 (crt0.S, same as the co-sim bootrom). */
ENTRY(_start)

SECTIONS
{
  .text.init 0x80000000 : { *(.text.init) }

  .text 0x80002000 : {
    *(.text .text.*)
    *(.rodata .rodata.* .srodata .srodata.*)
  }

  .data : ALIGN(16) { *(.data .data.*) }

  .sdata : ALIGN(16) {
    __global_pointer$ = . + 0x800;
    *(.sdata .sdata.*)
  }

  .bss : ALIGN(16) {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  }

  ASSERT(__bss_end < 0x8000E000, "CoreMark image too large: collides with stack at 0x8000F000")

  /DISCARD/ : { *(.comment) *(.note*) *(.riscv.attributes) *(.eh_frame*) }
}
