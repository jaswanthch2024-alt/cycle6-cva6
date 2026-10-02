/* crt0.S -- startup for the shared VP / CVA6 co-sim CoreMark build.
 * Sets gp and sp (same stack top as the co-sim bootrom), zeroes .bss
 * (the co-sim memory model expects every address written before it is
 * read), runs main, then stops the VP via HTIF tohost. */
    .section .text.init, "ax"
    .globl _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    li      sp, 0x8000F000
    li      t0, 0x6000          /* mstatus.FS = Dirty: enable the FPU (RTL resets it to Off) */
    csrs    mstatus, t0
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:  call    main
    li      t0, 0x80001000      /* HTIF tohost: VP stop (co-sim already stopped) */
    li      t1, 1
    sd      t1, 0(t0)
3:  j       3b
