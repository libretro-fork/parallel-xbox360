/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"
#include "xenia/base/platform.h"

#if XE_ARCH_ARM64 && XE_CLOCK_RAW_AVAILABLE

#include "xenia/base/arm64_sysreg.h"

namespace xe {
XE_NOINLINE
uint64_t Clock::host_tick_frequency_raw() { return xe_arm64_read_cntfrq_el0(); }
XE_NOINLINE
uint64_t Clock::host_tick_count_raw() { return xe_arm64_read_cntvct_el0(); }

}  // namespace xe

#endif
