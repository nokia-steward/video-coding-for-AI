/*
 * aarch64 CPU flags for OpenHEVC/FFmpeg libavutil.
 * Stub implementation so libLibOpenHevcWrapper.so defines ff_get_cpu_flags_aarch64
 * (libavutil/cpu.c calls it when ARCH_AARCH64). Return 0 = no optional features.
 */
#include "config.h"
#include "libavutil/cpu_internal.h"

int ff_get_cpu_flags_aarch64(void)
{
    return 0;
}
