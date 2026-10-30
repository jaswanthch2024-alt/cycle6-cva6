/*
 * robust_stress_bare.c — branch + RAW hazard stress, bare-metal for CVA6 co-sim
 * No stack arrays, no globals — pure register computation.
 * Stresses: LCG random branches (~50% mispredict), RAW dependency chains, MUL.
 */

typedef long word_t;
typedef unsigned int uint32_t;

void main(void) {
    asm volatile (".option norelax\n\tla gp, __global_pointer$");

    /* Program HPM event selectors */
    asm volatile ("csrw mhpmevent3,  %0" :: "r"(0x09)); /* branches        */
    asm volatile ("csrw mhpmevent4,  %0" :: "r"(0x0A)); /* mispredicts     */
    asm volatile ("csrw mhpmevent5,  %0" :: "r"(0x01)); /* I$ misses       */
    asm volatile ("csrw mhpmevent6,  %0" :: "r"(0x02)); /* D$ misses       */
    asm volatile ("csrw mhpmevent7,  %0" :: "r"(0x10)); /* I$ accesses     */
    asm volatile ("csrw mhpmevent8,  %0" :: "r"(0x11)); /* D$ accesses     */

    /* Zero the counters */
    asm volatile ("csrw mhpmcounter3, zero");
    asm volatile ("csrw mhpmcounter4, zero");
    asm volatile ("csrw mhpmcounter5, zero");
    asm volatile ("csrw mhpmcounter6, zero");
    asm volatile ("csrw mhpmcounter7, zero");
    asm volatile ("csrw mhpmcounter8, zero");

    word_t acc = 0;
    word_t val = 0xCAFEBABEL;
    word_t j, i;

    for (j = 0; j < 500; j++) {
        for (i = 0; i < 500; i++) {
            val = val * 1103515245 + 12345;
            if ((val >> 8) & 1) {
                acc += val;
                acc ^= 0xDEADBEEFL;
            } else {
                acc -= val;
                acc ^= 0xCAFEBABEL;
            }
            acc = acc + (val << 1) - (acc >> 3);
        }
    }

    asm volatile ("" : : "r"(acc));

    /* Read all counters */
    unsigned long long instret, cycles;
    unsigned long long branches, mispredicts, imiss, dmiss, iaccesses, daccesses;
    asm volatile ("csrr %0, minstret"      : "=r"(instret));
    asm volatile ("csrr %0, mcycle"        : "=r"(cycles));
    asm volatile ("csrr %0, mhpmcounter3"  : "=r"(branches));
    asm volatile ("csrr %0, mhpmcounter4"  : "=r"(mispredicts));
    asm volatile ("csrr %0, mhpmcounter5"  : "=r"(imiss));
    asm volatile ("csrr %0, mhpmcounter6"  : "=r"(dmiss));
    asm volatile ("csrr %0, mhpmcounter7"  : "=r"(iaccesses));
    asm volatile ("csrr %0, mhpmcounter8"  : "=r"(daccesses));

    volatile unsigned long long *halter = (volatile unsigned long long *)0x90000018ULL;
    *halter = cycles;
    *halter = instret;
    *halter = branches;
    *halter = mispredicts;
    *halter = imiss;
    *halter = dmiss;
    *halter = iaccesses;
    *halter = daccesses;
    *halter = 0xE000000CULL;
    *(volatile unsigned long long *)0x80001000ULL = 1;
}
