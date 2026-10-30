/*
 * fp_stress_bare.c — register-only double-precision FP stress, bare-metal.
 * The FP twin of robust_stress: no memory traffic in the loop, so it isolates
 * the FPU (fadd/fmul/fdiv/fsqrt) without load_hit_penalty or cold-start noise.
 * Reports cycles, minstret, HPM counters, and the bit-exact final result so we
 * can compare CVA6 (FPnew) vs VP (native host FP) for numerical correctness.
 *
 * Compile: -march=rv64g -mabi=lp64d -O1   (hardware F+D, doubles in FP regs)
 */

typedef unsigned long long u64;

static inline double fsqrt_d(double x) {
    double r;
    asm volatile ("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
    return r;
}

#define ITER 100000

void main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");

    /* Enable the FPU: set mstatus.FS = Dirty (bits 14:13), else FP ops trap. */
    asm volatile ("li t0, 0x6000\n\tcsrs mstatus, t0" ::: "t0");

    /* HPM event selectors (config A) */
    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09)); /* branches    */
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A)); /* mispredicts */
    asm volatile ("csrw mhpmevent5,  %0" :: "r"(0x01)); /* I$ misses   */
    asm volatile ("csrw mhpmevent6,  %0" :: "r"(0x02)); /* D$ misses   */
    asm volatile ("csrw mhpmevent7,  %0" :: "r"(0x10)); /* I$ accesses */
    asm volatile ("csrw mhpmevent8,  %0" :: "r"(0x11)); /* D$ accesses */
    asm volatile ("csrw mhpmcounter3, zero");
    asm volatile ("csrw mhpmcounter4, zero");
    asm volatile ("csrw mhpmcounter5, zero");
    asm volatile ("csrw mhpmcounter6, zero");
    asm volatile ("csrw mhpmcounter7, zero");
    asm volatile ("csrw mhpmcounter8, zero");

    /* Register-only FP recurrence: fmul, fadd, fdiv, fsqrt every iteration.
       Designed to stay bounded and converge to a stable, rounding-sensitive
       fixed point so the final bits exercise IEEE-754 consistency. */
    double acc = 1.5;
    double k   = 1.0000003;
    for (long i = 0; i < ITER; i++) {
        acc = acc * k + 0.1;          /* fmul + fadd */
        acc = fsqrt_d(acc) + acc / 7.0; /* fsqrt + fdiv + fadd */
        if (acc > 1.0e6) acc = acc / 1.0e6; /* keep bounded (fdiv) */
    }

    /* Read counters (before any halter store consumes cycles). */
    u64 cycles, instret, branches, mispredicts, imiss, dmiss, iacc, dacc;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(branches));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mispredicts));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(imiss));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dmiss));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(iacc));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(dacc));

    /* Raw bit pattern of the final double — the correctness signature. */
    u64 result_bits;
    asm volatile ("fmv.x.d %0, %1" : "=r"(result_bits) : "f"(acc));

    volatile u64 *halter = (volatile u64 *)0x90000018ULL;
    *halter = cycles;
    *halter = instret;
    *halter = branches;
    *halter = mispredicts;
    *halter = imiss;
    *halter = dmiss;
    *halter = iacc;
    *halter = dacc;
    *halter = result_bits;            /* <-- compare this between CVA6 and VP */
    *halter = 0xE000000CULL;
    /* VP clean halt via HTIF; also echo result so we can read it on the VP. */
    *(volatile u64 *)0x80001000ULL = result_bits;
}
