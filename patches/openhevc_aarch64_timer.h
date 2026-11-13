/*
 * aarch64 timer for OpenHEVC/FFmpeg libavutil (no x86 asm on aarch64).
 * Used when building OpenHEVC on aarch64; provides AV_READ_TIME via gettimeofday.
 */
#ifndef AVUTIL_AARCH64_TIMER_H
#define AVUTIL_AARCH64_TIMER_H

#include <stdint.h>
#include "config.h"

#if HAVE_GETTIMEOFDAY
#include <sys/time.h>

#define FF_TIMER_UNITS "us"
#define AV_READ_TIME read_time

static inline uint64_t read_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
}
#endif

#endif /* AVUTIL_AARCH64_TIMER_H */
