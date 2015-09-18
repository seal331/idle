#ifndef CONFIGH
#define CONFIGH

#ifdef __GNUC__
#ifndef INLINE
#define INLINE static inline
#endif
#endif

/*
 * compiler representation of M68000 .B .W .L operands
 */
typedef signed char     int8;
typedef signed short    int16;
typedef signed long     int32;
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned long   uint32;

#define CHKADDRESSERR       /* if set, unaligned access will raise
                             * an address error (slower, but
                             * expected behaviour) */

#undef DEBUG                /* Debug */

#define _ACCURATE_TIMERS_
// enables a "PC when address written" debug
// may be useful in very low level debugging...
// #define _MEMORY_WRITE_DEBUG_

// by default error log is memory only
//#define _TRACE_LOG_FILE_

// enable MMU debug
// (disabled to speed up the emulator)
// #define _DEBUG_MMU_
// musashi compatibility macros
#define get_pc m68k_get_reg(NULL,M68K_REG_PC)
#define GetS() (m68ki_cpu.s_flag)


// PC value for breakpoint (debugger)
extern int ask_bkpt;

/*
 * Debug options
 */
#define CHKADDRESSERR       /* force address error checking */
#define ON_TRAP(number)
#define ON_UNMAPPED(address, value)
#define ON_NOIO(address, value)
#define ON_WRITE(address, value)

extern char *log_box;
extern char *mmu_box;

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "debug/trace.h"



#endif /* CONFIGH */

