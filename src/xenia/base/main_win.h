/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_WIN_H_
#define XENIA_BASE_MAIN_WIN_H_

#include <string_view>

namespace xe {

// Libretro-owned startup configures cvars before host initialization.
int InitializeWin32App(const std::string_view app_name);
void ShutdownWin32App();

}  // namespace xe

#endif  // XENIA_BASE_MAIN_WIN_H_
