/*
 * dynamic_access_bench.c — PHASE-SHIFTING hot-region test.
 *
 * Unlike locality_bench (one fixed hot region, forever) and mem_contention_bench
 * (one endless scan, never revisited), real programs shift their working set
 * over time -- one data structure is hot for a while, then the program moves
 * on to another. This benchmark models that directly:
 *
 *   - The D$ working set is split into NUM_PHASES disjoint sub-regions.
 *   - Within a phase, its region is hammered REVISITS times in a row --
 *     genuinely locality-friendly, exactly like locality_bench's hot region.
 *   - Then the program moves to the NEXT phase's (different) region, and the
 *     previous phase goes cold.
 *   - After all phases, the whole thing repeats for ROUNDS rounds -- so a
 *     phase that was hot several phases ago becomes hot again.
 *
 * Total footprint (NUM_PHASES * PHASE_LINES * 16B) is ~1.5x the 32KB D$, so
 * a phase transition forces genuine eviction, and by the time a phase's
 * region comes back around on the next round, it has almost certainly been
 * evicted by the other phases in between -- unless the policy is somehow
 * still "remembering" it.
 *
 * This is the concrete test for a specific question about LFU: does giving
 * disproportionate weight to *historical* access frequency mean it clings to
 * a once-hot phase's data (cache pollution) instead of cleanly adapting to
 * the new phase, the way a purely recency-based policy (LRU/NRU) would?
 * locality_bench and mem_contention_bench can't distinguish this -- both
 * have a SINGLE, unchanging notion of "hot," so frequency and recency never
 * disagree with each other there. This benchmark is designed so they can.
 *
 * Compile: -fPIC -march=rv64g -mabi=lp64f -O1 -fno-builtin, linked at 0x80000000.
 */
typedef unsigned long long u64;

#define DATA_BASE   0x80300000ULL
#define NUM_PHASES  4
#define PHASE_LINES 768               /* 768*16B = 12 KB per phase */
#define PHASE_BYTES (PHASE_LINES * 16ULL)
#define TOTAL_BYTES (NUM_PHASES * PHASE_BYTES)   /* 48 KB > 32 KB D$ */
#define REVISITS    16                 /* hammer a phase's region this many times */
#define ROUNDS      3                  /* cycle through all phases this many times */

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

    /* populate the whole region so co-sim memory is mapped */
    for (u64 i = 0; i < TOTAL_BYTES / 16ULL; i++)
        *(volatile u64 *)(DATA_BASE + i * 16ULL) = i * 2654435761ULL;

    u64 sum = 0;
    for (u64 r = 0; r < ROUNDS; r++) {
        for (u64 p = 0; p < NUM_PHASES; p++) {
            u64 phase_base = DATA_BASE + p * PHASE_BYTES;
            for (u64 v = 0; v < REVISITS; v++) {
                for (u64 i = 0; i < PHASE_LINES; i++) {
                    sum += *(volatile u64 *)(phase_base + i * 16ULL);
                }
            }
        }
    }

    u64 cycles, instret, br, mp, im, dm, ia, da;
    asm volatile ("csrr %0, mcycle"       : "=r"(cycles));
    asm volatile ("csrr %0, minstret"     : "=r"(instret));
    asm volatile ("csrr %0, mhpmcounter3" : "=r"(br));
    asm volatile ("csrr %0, mhpmcounter4" : "=r"(mp));
    asm volatile ("csrr %0, mhpmcounter5" : "=r"(im));
    asm volatile ("csrr %0, mhpmcounter6" : "=r"(dm));
    asm volatile ("csrr %0, mhpmcounter7" : "=r"(ia));
    asm volatile ("csrr %0, mhpmcounter8" : "=r"(da));
    u64 result = sum;

    volatile u64 *h = (volatile u64 *)0x90000018ULL;
    *h = cycles; *h = instret; *h = br; *h = mp; *h = im; *h = dm; *h = ia; *h = da;
    *h = result;
    *h = 0xE000000CULL;
    *(volatile u64 *)0x80001000ULL = result;   /* VP clean halt (HTIF) */
}
