/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <CoreFoundation/CoreFoundation.h>
#include "xenia/base/system.h"
namespace xe {
bool SetProcessPriorityClass(const uint32_t priority_class) { return true; }

bool ExecutableHasX86_64Slice() {
  static const bool has_slice = [] {
    CFArrayRef archs =
        CFBundleCopyExecutableArchitectures(CFBundleGetMainBundle());
    if (!archs) {
      return false;
    }
    int32_t x86_64 = kCFBundleExecutableArchitectureX86_64;
    CFNumberRef x86_64_number =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &x86_64);
    bool result = CFArrayContainsValue(
        archs, CFRangeMake(0, CFArrayGetCount(archs)), x86_64_number);
    CFRelease(x86_64_number);
    CFRelease(archs);
    return result;
  }();
  return has_slice;
}

}  // namespace xe
