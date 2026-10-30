/*
 * fp_bench.c — clean floating-point throughput, bare-metal for VP + CVA6.
 * Register-only, single predictable loop branch (no random branches, no memory).
 * Body is a dependency chain of fadd + fmul on doubles, with contracting
 * coefficients (|coeff| < 1) so values stay bounded — no inf/nan, deterministic.
 * Isolates the FP add/mul pipeline with zero confounders.
 * Compile: -mcmodel=medany -march=rv64g -mabi=lp64d -O1 -ffp-contract=off
 */
typedef unsigned long long u64;

#define ITER 100000

void main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");
    /* enable FPU (mstatus.FS = Dirty) */
    asm volatile ("li t0, 0x6000\n\tcsrs mstatus, t0" ::: "t0");
    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A));
    asm volatile ("csrw mhpmevent5,  %0" :: "r"(0x01));
    asm volatile ("csrw mhpmevent6,  %0" :: "r"(0x02));
    asm volatile ("csrw mhpmevent7,  %0" :: "r"(0x10));
    asm volatile ("csrw mhpmevent8,  %0" :: "r"(0x11));
    asm volatile ("csrw mhpmcounter3, zero"); asm volatile ("csrw mhpmcounter4, zero");
    asm volatile ("csrw mhpmcounter5, zero"); asm volatile ("csrw mhpmcounter6, zero");
    asm volatile ("csrw mhpmcounter7, zero"); asm volatile ("csrw mhpmcounter8, zero");

    double x = 1.1, y = 2.2, z = 3.3, w = 4.4;
    for (u64 i = 0; i < ITER; i++) {
        x = x * 0.9999 + 0.1;        /* fmul + fadd */
        y = y * 0.9998 + z * 0.01;   /* fmul + fmul + fadd */
        z = z * 0.9997 + w * 0.01;
        w = w * 0.9996 + x * 0.01;
    }
    asm volatile ("" : : "r"((u64)(x + y + z + w)));

    u64 cycles, instret, br, mp, im, dm, ia, da;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(im));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dm));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(ia));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(da));
    double acc = x + y + z + w;
    u64 result; asm volatile ("fmv.x.d %0, %1" : "=r"(result) : "f"(acc));

    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = im; *h = dm; *h = ia; *h = da;
    *h = result;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = result;   /* VP clean halt (HTIF) */
}
