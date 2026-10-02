/*
 * dmiss.c -- isolated D$ miss cost, same ELF on VP and CVA6 RTL co-sim.
 * Walks a 256 KB region (8x the 32 KB D$) in STRIDE-byte steps so nearly every
 * 16 B line is touched once per pass (cold or capacity miss). MODE selects:
 *   0: load, then immediately add it to a sum (load -> use, serialised)
 *   1: load only every access; sum folded once per 8 loads (misses back-to-back)
 * Result port: cycles, instret, D$ misses, D$ accesses (timed loop only).
 */
typedef unsigned long u64;
#ifndef STRIDE
#define STRIDE 16
#endif
#ifndef MODE
#define MODE 0
#endif
#define N 20000
#define REGION 0x40000UL
#define BASE 0x80100000UL

#define CSRR(c) ({ u64 v_; asm volatile ("csrr %0, " #c : "=r"(v_)); v_; })

int main(void)
{
    asm volatile ("csrw mhpmevent3, %0" :: "r"(0x02));  /* D$ miss   */
    asm volatile ("csrw mhpmevent4, %0" :: "r"(0x11));  /* D$ access */
    asm volatile ("csrw mhpmcounter3, zero");
    asm volatile ("csrw mhpmcounter4, zero");
    volatile u64 *p = (volatile u64 *)BASE;
    u64 sum = 0, off = 0;
    u64 c0 = CSRR(mcycle), i0 = CSRR(minstret);
#if MODE == 0
    for (u64 i = 0; i < N; i++) {
        sum += *(volatile u64 *)((u64)p + off);
        off = (off + STRIDE) & (REGION - 1);
    }
#else
    for (u64 i = 0; i < N; i += 8) {
        u64 a = *(volatile u64 *)((u64)p + ((off + 0 * STRIDE) & (REGION - 1)));
        u64 b = *(volatile u64 *)((u64)p + ((off + 1 * STRIDE) & (REGION - 1)));
        u64 c = *(volatile u64 *)((u64)p + ((off + 2 * STRIDE) & (REGION - 1)));
        u64 d = *(volatile u64 *)((u64)p + ((off + 3 * STRIDE) & (REGION - 1)));
        u64 e = *(volatile u64 *)((u64)p + ((off + 4 * STRIDE) & (REGION - 1)));
        u64 f = *(volatile u64 *)((u64)p + ((off + 5 * STRIDE) & (REGION - 1)));
        u64 g = *(volatile u64 *)((u64)p + ((off + 6 * STRIDE) & (REGION - 1)));
        u64 h = *(volatile u64 *)((u64)p + ((off + 7 * STRIDE) & (REGION - 1)));
        sum += a + b + c + d + e + f + g + h;
        off = (off + 8 * STRIDE) & (REGION - 1);
    }
#endif
    u64 c1 = CSRR(mcycle), i1 = CSRR(minstret);
    u64 m = CSRR(mhpmcounter3), a = CSRR(mhpmcounter4);
    volatile u64 *h = (volatile u64 *)0x90000018UL;
    *h = c1 - c0; *h = i1 - i0; *h = m; *h = a; *h = sum;
    *h = 0xE000000CUL;
    return 0;
}
