/*
 * int_indep_bench.c — DISCRIMINATING EXPERIMENT.
 *
 * Identical in structure to int_nobranch_bench (straight-line unrolled ALU,
 * ~5 KB of code, one loop branch per 1024 ops) EXCEPT the dependency chain is
 * broken: 8 independent variables are interleaved, so no instruction depends on
 * the instruction immediately before it (each var's ops are 8 apart).
 *
 * Purpose: distinguish two hypotheses for CVA6's ~0.19 cyc/instr overhead on
 * straight-line ALU code:
 *   H1 (dependency bubble): CPI should DROP to ~1.0 here (deps are spaced out).
 *   H2 (fetch bandwidth / I$ line crossing): CPI should STAY ~1.2 (same fetch
 *       pattern, same code size).
 *
 * Ops cycle through + ^ - | so the compiler cannot constant-fold the chains.
 * Compile: -fPIC -march=rv64g -mabi=lp64f -O1 -fno-builtin
 */
typedef unsigned long long u64;

#define ITER 2500

/* Each block: 8 independent single-instruction ops. A given variable's next op
   is 8 instructions later -> no back-to-back dependency. */
#define A8  v0+=1;  v1^=5;  v2-=3;  v3|=9;  v4+=7;  v5^=11; v6-=13; v7|=15;
#define B8  v0^=3;  v1-=7;  v2|=1;  v3+=5;  v4^=2;  v5+=9;  v6|=4;  v7-=6;
#define C8  v0-=2;  v1|=6;  v2+=8;  v3^=4;  v4-=1;  v5|=3;  v6+=5;  v7^=7;
#define D8  v0|=8;  v1+=2;  v2^=6;  v3-=4;  v4|=5;  v5-=3;  v6^=9;  v7+=1;
#define OP32   A8 B8 C8 D8
#define OP128  OP32 OP32 OP32 OP32
#define OP1024 OP128 OP128 OP128 OP128 OP128 OP128 OP128 OP128

void main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");
    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09));
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A));
    asm volatile ("csrw mhpmevent5,  %0" :: "r"(0x01));
    asm volatile ("csrw mhpmevent6,  %0" :: "r"(0x02));
    asm volatile ("csrw mhpmevent7,  %0" :: "r"(0x10));
    asm volatile ("csrw mhpmevent8,  %0" :: "r"(0x11));
    asm volatile ("csrw mhpmcounter3, zero"); asm volatile ("csrw mhpmcounter4, zero");
    asm volatile ("csrw mhpmcounter5, zero"); asm volatile ("csrw mhpmcounter6, zero");
    asm volatile ("csrw mhpmcounter7, zero"); asm volatile ("csrw mhpmcounter8, zero");

    u64 v0=0x11, v1=0x22, v2=0x33, v3=0x44, v4=0x55, v5=0x66, v6=0x77, v7=0x88;

    for (u64 i = 0; i < ITER; i++) {
        OP1024              /* 1024 INDEPENDENT ops, ONE loop branch */
    }
    asm volatile ("" :: "r"(v0),"r"(v1),"r"(v2),"r"(v3),"r"(v4),"r"(v5),"r"(v6),"r"(v7));

    u64 cycles, instret, br, mp, im, dm, ia, da;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(im));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dm));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(ia));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(da));
    u64 result = v0^v1^v2^v3^v4^v5^v6^v7;

    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = im; *h = dm; *h = ia; *h = da;
    *h = result;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = result;   /* VP clean halt (HTIF) */
}
