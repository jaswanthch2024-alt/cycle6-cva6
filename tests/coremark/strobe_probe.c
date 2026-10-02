/*
 * strobe_probe.c -- does the CVA6 co-sim memory path honour AXI write
 * strobes for sub-word stores? Same ELF on VP and co-sim (crt0.S, link.ld).
 *
 * Each probe word starts as 0x1122334455667788 (loaded from the image) and
 * sits in its own 64-byte block, so the first access to it is the store:
 * a store miss in CVA6's write-through D$ goes straight to memory, and the
 * following load misses and refills from memory -- i.e. it reads back what
 * memory actually holds after the narrow write.
 *
 * Result port: for each case, the value read back, then the expected value.
 */
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define INIT 0x1122334455667788UL
struct blk { volatile u64 w; u64 pad[7]; } __attribute__((aligned(64)));
static struct blk p[6] = {
    {INIT, {0}}, {INIT, {0}}, {INIT, {0}}, {INIT, {0}}, {INIT, {0}}, {INIT, {0}},
};

int main(void)
{
    volatile u64 *h = (volatile u64 *)0x90000018UL;
    u64 got[6];

    *((volatile u8  *)&p[0].w + 1) = 0xAA;              /* 8-bit  at +1 */
    *((volatile u16 *)&p[1].w + 1) = 0xBBBB;            /* 16-bit at +2 */
    *((volatile u32 *)&p[2].w + 1) = 0xCCCCCCCCu;       /* 32-bit at +4 */
    *((volatile u32 *)&p[3].w + 0) = 0xDDDDDDDDu;       /* 32-bit at +0 */
    p[4].w = 0xEEEEEEEEEEEEEEEEUL;                      /* 64-bit       */
    (void)0;                                            /* p[5]: no store */
    asm volatile ("fence rw, rw" ::: "memory");
    for (int i = 0; i < 6; i++) got[i] = p[i].w;

    const u64 expect[6] = {
        0x112233445566AA88UL, 0x11223344BBBB7788UL, 0xCCCCCCCC55667788UL,
        0x11223344DDDDDDDDUL, 0xEEEEEEEEEEEEEEEEUL, INIT,
    };
    for (int i = 0; i < 6; i++) { *h = got[i]; *h = expect[i]; }
    *h = 0xE000000CUL;
    return 0;
}
