/*
 * multiply_bare.c — riscv-tests "multiply" software multiply (32-iter shift-add),
 * wrapped in the bare-metal HTIF harness used by the other VP/co-sim benchmarks.
 *
 * The kernel (multiply.c) and dataset (dataset1.h) are the unmodified riscv-tests
 * sources; only the harness is added, so the same ELF runs on both the VP and the
 * CVA6 co-sim. Repeated REPS times so the timed region dominates cold-start.
 *
 * Compile: -fPIC -march=rv64g -mabi=lp64f -O2 -fno-builtin
 *   -I<riscv-tests>/benchmarks/multiply
 */
typedef unsigned long long u64;

#define REPS 100
#include "multiply.c"     /* int multiply(int x, int y) */
#include "dataset1.h"     /* input_data1[DATA_SIZE], input_data2[DATA_SIZE], verify_data */

static int results_data[DATA_SIZE];

void __attribute__((section(".text.main"))) main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");
    asm volatile ("li sp, 0x8f000000");   /* stack near top of DRAM (bare-metal: no crt0) */
    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A));
    asm volatile ("csrw mhpmevent5,  %0" :: "r"(0x01));
    asm volatile ("csrw mhpmevent6,  %0" :: "r"(0x02));
    asm volatile ("csrw mhpmevent7,  %0" :: "r"(0x10));
    asm volatile ("csrw mhpmevent8,  %0" :: "r"(0x11));
    asm volatile ("csrw mhpmcounter3, zero"); asm volatile ("csrw mhpmcounter4, zero");
    asm volatile ("csrw mhpmcounter5, zero"); asm volatile ("csrw mhpmcounter6, zero");
    asm volatile ("csrw mhpmcounter7, zero"); asm volatile ("csrw mhpmcounter8, zero");

    for (int r = 0; r < REPS; r++)
        for (int i = 0; i < DATA_SIZE; i++)
            results_data[i] = multiply(input_data1[i], input_data2[i]);

    u64 chk = 0;
    int ok = 1;
    for (int i = 0; i < DATA_SIZE; i++) {
        chk = chk * 1000003ULL + (u64)(unsigned)results_data[i];
        if (results_data[i] != verify_data[i]) ok = 0;
    }
    asm volatile ("" :: "r"(chk));

    u64 cycles, instret, br, mp, im, dm, ia, da;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(im));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dm));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(ia));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(da));
    u64 result = chk;   /* report raw checksum always (diagnostic) */
    asm volatile ("" :: "r"(ok));

    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = im; *h = dm; *h = ia; *h = da;
    *h = result;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = result;   /* VP clean halt (HTIF) */
}
