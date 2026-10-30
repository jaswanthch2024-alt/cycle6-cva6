/*
 * int_alu_bench.c — clean integer-ALU throughput, bare-metal for VP + CVA6.
 * Register-only, single predictable loop branch (no random branches, no memory).
 * Body is a dependency chain of 1-cycle ALU ops (add/sub/and/or/xor/shift) so
 * both models are limited by the same latency — isolates the basic integer
 * pipeline with zero confounders (no loads, no mispredicts, no cache traffic).
 * Compile: -fPIC -march=rv64g -mabi=lp64f -O1 -fno-builtin
 */
typedef unsigned long long u64;

#define ITER 200000

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

    u64 a = 0x1111, b = 0x2222, c = 0x3333, d = 0x4444;
    for (u64 i = 0; i < ITER; i++) {
        a = a + b;            /* add   */
        b = b ^ c;            /* xor   */
        c = c - d;            /* sub   */
        d = d | (a << 3);     /* or + shift */
        a = a & (b >> 2);     /* and + shift */
        b = b + (c ^ d);      /* add + xor  */
    }
    asm volatile ("" : : "r"(a), "r"(b), "r"(c), "r"(d));

    u64 cycles, instret, br, mp, im, dm, ia, da;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(im));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dm));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(ia));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(da));
    u64 result = a ^ b ^ c ^ d;

    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = im; *h = dm; *h = ia; *h = da;
    *h = result;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = result;   /* VP clean halt (HTIF) */
}
