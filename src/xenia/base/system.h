/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_SYSTEM_H_
#define XENIA_BASE_SYSTEM_H_

#include <filesystem>
#include <string_view>

#include "xenia/base/platform.h"
#include "xenia/base/string.h"

namespace xe {


bool SetProcessPriorityClass(const uint32_t priority_class);

#if XE_PLATFORM_MAC
// Whether the running executable carries an x86_64 slice Rosetta can launch.
bool ExecutableHasX86_64Slice();
#endif

}  // namespace xe

#endif  // XENIA_BASE_SYSTEM_H_
