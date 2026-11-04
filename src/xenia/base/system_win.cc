/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <map>
#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"

namespace xe {

static std::map<const uint32_t, DWORD> xeniaToWindowsPriorityClassMapping = {
    {0, NORMAL_PRIORITY_CLASS},
    {1, ABOVE_NORMAL_PRIORITY_CLASS},
    {2, HIGH_PRIORITY_CLASS},
    {3, REALTIME_PRIORITY_CLASS}};

bool SetProcessPriorityClass(const uint32_t priority_class) {
  if (!xeniaToWindowsPriorityClassMapping.count(priority_class)) {
    return false;
  }

  return SetPriorityClass(GetCurrentProcess(),
                          xeniaToWindowsPriorityClassMapping[priority_class]);
}

}  // namespace xe
