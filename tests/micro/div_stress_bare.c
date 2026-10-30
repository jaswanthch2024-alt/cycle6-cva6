/*
 * div_stress_bare.c — register-only 64-bit integer division stress, bare-metal.
 * The DIV twin of robust_stress: no memory traffic, so it isolates the divider
 * (div/rem) and makes the 66-cycle DIV latency dominate. Runs on both the CVA6
 * co-sim and the VP. Reports HPM counters + the bit-exact final result.
 *
 * Divisors are always forced odd/non-zero to avoid the div-by-zero fast path.
 * Compile: -fPIC -march=rv64g -mabi=lp64f -O1 -fno-builtin
 */

typedef unsigned long long u64;
typedef long word_t;

#define ITER 10000

void main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");

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

    word_t a = 0x123456789ABCL;
    word_t b = 0x0CAFEBABEL;
    for (word_t i = 0; i < ITER; i++) {
        word_t da = (b & 0xFFFF) | 1;          /* odd, non-zero divisor */
        word_t q  = (a + i * 31) / da;         /* 64-bit DIV  */
        word_t db = (q & 0x3FFF) | 1;          /* odd, non-zero divisor */
        word_t r  = (b ^ q) % db;              /* 64-bit REM  */
        a = a * 3 + r + 7;
        b = (q ^ r) + i;
        if (a < 0) a = -a;
    }

    asm volatile ("" : : "r"(a), "r"(b));

    u64 cycles, instret, branches, mispredicts, imiss, dmiss, iacc, dacc;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(branches));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mispredicts));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(imiss));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dmiss));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(iacc));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(dacc));

    u64 result = (u64)a ^ ((u64)b << 1);

    volatile u64 *halter = (volatile u64 *)0x90000018ULL;
    *halter = cycles;
    *halter = instret;
    *halter = branches;
    *halter = mispredicts;
    *halter = imiss;
    *halter = dmiss;
    *halter = iacc;
    *halter = dacc;
    *halter = result;                 /* <-- compare between CVA6 and VP */
    *halter = 0xE000000CULL;
    /* VP clean halt via HTIF; echoes result so we can read it on the VP. */
    *(volatile u64 *)0x80001000ULL = result;
}
