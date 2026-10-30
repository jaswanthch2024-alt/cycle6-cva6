ENTRY(main)

MEMORY
{
  DRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 0x10000000
}

SECTIONS
{
  .text :
  {
    *(.text.main)
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  } > DRAM

  .sdata :
  {
    __global_pointer$ = . + 0x800;
    *(.sdata)
    *(.sdata.*)
  } > DRAM

  .sbss :
  {
    *(.sbss)
    *(.sbss.*)
  } > DRAM

  .data :
  {
    *(.data)
    *(.data.*)
  } > DRAM

  .bss :
  {
    *(.bss)
    *(.bss.*)
    *(COMMON)
  } > DRAM
}
