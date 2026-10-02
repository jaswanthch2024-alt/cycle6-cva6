/*
 * core_portme.c -- bare-metal CoreMark port for the Cycle6 VP and the CVA6
 * RTL co-simulation (see core_portme.h).
 *
 * Timing: start_time()/stop_time() snapshot mcycle, minstret and HPM3-8
 * around CoreMark's timed region (iterate() only, init excluded).
 *
 * Validation: CoreMark checks its own CRCs and reports through ee_printf.
 * There is no console, so printf() below only captures the values that
 * matter (seedcrc, crcfinal, CRC error lines) for the result port.
 *
 * Result port writes (0x90000018), in order:
 *   0 cycles  1 instret  2 branches  3 mispredicts
 *   4 I$ miss 5 D$ miss  6 I$ acc    7 D$ acc        (all timed-region deltas)
 *   8 status = crc_errors << 32 | seedcrc << 16 | crcfinal
 *   then 0xE000000C (co-sim stop). crt0 then writes HTIF tohost (VP stop).
 */
#include "coremark.h"

ee_u32 default_num_contexts = 1;

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

#define CSRR(csr) ({ ee_u64 v_; asm volatile ("csrr %0, " #csr : "=r"(v_)); v_; })

static ee_u64 snap_start[8], snap_stop[8];

static void snapshot(ee_u64 *s)
{
    s[0] = CSRR(mcycle);
    s[1] = CSRR(minstret);
    s[2] = CSRR(mhpmcounter3);
    s[3] = CSRR(mhpmcounter4);
    s[4] = CSRR(mhpmcounter5);
    s[5] = CSRR(mhpmcounter6);
    s[6] = CSRR(mhpmcounter7);
    s[7] = CSRR(mhpmcounter8);
}

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;
    /* Same HPM event mapping as the other co-sim benchmarks. */
    asm volatile ("csrw mhpmevent3, %0" :: "r"(0x09)); /* branches    */
    asm volatile ("csrw mhpmevent4, %0" :: "r"(0x0A)); /* mispredicts */
    asm volatile ("csrw mhpmevent5, %0" :: "r"(0x01)); /* I$ miss     */
    asm volatile ("csrw mhpmevent6, %0" :: "r"(0x02)); /* D$ miss     */
    asm volatile ("csrw mhpmevent7, %0" :: "r"(0x10)); /* I$ access   */
    asm volatile ("csrw mhpmevent8, %0" :: "r"(0x11)); /* D$ access   */
    p->portable_id = 1;
}

void start_time(void) { snapshot(snap_start); }
void stop_time(void)  { snapshot(snap_stop); }

CORE_TICKS get_time(void)
{
    return snap_stop[0] - snap_start[0];
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    /* No wall clock here; CoreMark's 10 s rule is irrelevant for a
     * cycle-count comparison. Report ticks as-is. */
    return (secs_ret)ticks;
}

/* ---- result capture (no console) ---- */

static ee_u32 cap_seedcrc  = 0;
static ee_u32 cap_crcfinal = 0xFFFF;
static ee_u32 cap_crc_errs = 0;

static int contains(const char *s, const char *key)
{
    for (; *s; s++) {
        const char *a = s, *b = key;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

int printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    if (contains(fmt, "crcfinal")) {
        (void)__builtin_va_arg(ap, int);              /* context index */
        cap_crcfinal = (ee_u32)__builtin_va_arg(ap, int) & 0xFFFF;
    } else if (contains(fmt, "seedcrc")) {
        cap_seedcrc = (ee_u32)__builtin_va_arg(ap, int) & 0xFFFF;
    } else if (contains(fmt, "ERROR!") && contains(fmt, "crc")) {
        cap_crc_errs++;                               /* list/matrix/state */
    }
    __builtin_va_end(ap);
    return 0;
}

void portable_fini(core_portable *p)
{
    volatile ee_u64 *h = (volatile ee_u64 *)0x90000018UL;
    for (int i = 0; i < 8; i++)
        *h = snap_stop[i] - snap_start[i];
    *h = ((ee_u64)cap_crc_errs << 32) | ((ee_u64)cap_seedcrc << 16) | cap_crcfinal;
    *h = 0xE000000CUL;
    p->portable_id = 0;
}
