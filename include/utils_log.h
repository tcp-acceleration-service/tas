#ifndef UTILS_LOG_H_
#define UTILS_LOG_H_

#include <rte_log.h>
#include <rte_cycles.h>

#include <stdio.h>
#include <stdarg.h>

/*
 * DPDK supports 8 user-defined log types.
 * TAS modules are mapped into user-defined types for:
 * (a) Helps identify the module from the log
 * (b) Module-wise filtering of logs may be implemented in the future
 */

#define _LOG_USER_START RTE_LOGTYPE_USER1
#define _LOG_USER_END   RTE_LOGTYPE_USER8

enum
{
    LOG_MAIN,
    LOG_FAST_TX,
    LOG_FAST_RX,
    LOG_FAST_QMAN,
    LOG_FAST_QMAN_FWD,
    LOG_FAST_APPIF,
    LOG_SLOW,
    // Add more if required !
    LOG_END
};

/* Useful macros */
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define TIMESTAMP_FMTSTR        "[%4.4u:%2.2u:%2.2u.%6.6u]"
#define FILE_LINENUM_FMTSTR     "[%15.15s:%4.4d]"
#define PAD_FMTSTR              " "
#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

static inline const char*
gettimestamp()
{
    static __thread char buf[20];

    // Prepare the timestamp
    uint64_t cyc = rte_get_tsc_cycles();
    static __thread uint64_t freq = 0;

    if (freq == 0)
        freq = rte_get_tsc_hz();

    uint64_t elapsed_time_secs = cyc / freq;
    cyc = cyc % freq;   // for sub-second values
    unsigned int hrs, mins, secs, microsecs;
    hrs = (unsigned) (elapsed_time_secs / (60 * 60));
    elapsed_time_secs -= (hrs * 60 * 60);
    mins = (unsigned) (elapsed_time_secs / 60);
    elapsed_time_secs -= (mins * 60);
    secs = (unsigned) elapsed_time_secs;
    microsecs = (unsigned) ((cyc * 1000000ULL) / freq);

    snprintf(buf, sizeof(buf), TIMESTAMP_FMTSTR, hrs, mins, secs, microsecs);

    return buf;
}

static inline int
tas_log(uint32_t level,
        uint32_t logtype,
        const char* format, ...)
{
    BUILD_BUG_ON((LOG_END + _LOG_USER_START) > _LOG_USER_END);

    va_list ap;
    int ret;

    va_start(ap, format);
    ret = rte_vlog(level, logtype, format, ap);
    va_end(ap);

    return ret;
}

#define TAS_LOG(l, t, fmtstr, ...) \
	(void)((RTE_LOG_ ## l <= RTE_LOG_DP_LEVEL) ?		\
        tas_log(RTE_LOG_ ## l,					\
            _LOG_USER_START + LOG_ ## t,        \
            "%s" FILE_LINENUM_FMTSTR PAD_FMTSTR # t ": " fmtstr,    \
            gettimestamp(),                     \
            __FILENAME__, __LINE__,             \
            __VA_ARGS__) :	\
	        0)

#endif /* UTILS_LOG_H_ */
