/*
 * long_test3_bare.c — multiply-accumulate stress, bare-metal for CVA6 co-sim
 * Ported from riscv-vp/tests/C/long_test3/long_test3.c
 * Removed: printf, ecall, infinite loop
 * Added: fixed iteration count, HPM counters, sim_halter exit
 *
 * HPM counter mapping (mhpmevent3-8):
 *   counter3 = 0x09  branches committed
 *   counter4 = 0x0A  branch mispredicts
 *   counter5 = 0x01  I$ misses
 *   counter6 = 0x02  D$ misses
 *   counter7 = 0x10  I$ accesses
 *   counter8 = 0x11  D$ accesses
 */

#define ITERATIONS 100000

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

    volatile int value1, value2, value3;
    value1 = 5;
    value2 = 7;
    value3 = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        value3 += value1 * value2;
    }

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

    /* Report via sim_halter (each write prints the value) */
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
