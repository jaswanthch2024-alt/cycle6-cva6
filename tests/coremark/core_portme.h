/*
 * core_portme.h -- bare-metal CoreMark port shared by the Cycle6 VP and the
 * CVA6 RTL co-simulation. Same ELF runs on both; no libc, no UART.
 *
 * Results leave through the 0x90000018 result port (sim_halter in the
 * co-sim, [HALTER] print in the VP), see core_portme.c.
 *
 * CoreMark sources (core_*.c, coremark.h) are unmodified copies from
 * cva6/verif/tests/custom/coremark (EEMBC, Apache-2.0, see LICENSE.md).
 * Runs here are cycle-count comparisons, not official CoreMark scores.
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>

#define HAS_FLOAT  0
#define HAS_TIME_H 0
#define USE_CLOCK  0
#define HAS_STDIO  0
#define HAS_PRINTF 1   /* ee_printf -> printf, implemented in core_portme.c */

#ifndef ITERATIONS
#define ITERATIONS 10
#endif

#define COMPILER_VERSION "GCC" __VERSION__
#ifndef FLAGS_STR
#define FLAGS_STR "unspecified"
#endif
#define COMPILER_FLAGS FLAGS_STR
#define MEM_LOCATION   "STATIC"
#define SC_MEM_LOCATION "UNSPECIFIED(" MEM_LOCATION ") RATIOS:1"

typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed int     ee_s32;
typedef unsigned char  ee_u8;
typedef unsigned int   ee_u32;
typedef unsigned long  ee_u64;
typedef ee_u64         ee_ptr_int;
typedef size_t         ee_size_t;
typedef ee_u64         CORE_TICKS;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD  MEM_STATIC

#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0

#define MAIN_HAS_NOARGC   1
#define MAIN_HAS_NORETURN 0

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

extern ee_u32 default_num_contexts;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

int printf(const char *fmt, ...);

#endif /* CORE_PORTME_H */
