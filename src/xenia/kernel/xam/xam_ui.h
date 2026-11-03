/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_UI_H_
#define XENIA_KERNEL_XAM_XAM_UI_H_

#include <functional>
#include <string>

#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace hid {
class InputSystem;
}  // namespace hid
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

// Told about each message box the libretro core answers without a window:
// its title and the button chosen, for an on-screen message
extern std::function<void(const std::string& title, const std::string& answer)>
    xam_message_box_notice;

// Picks an installed indie game, or reports that there are none.
bool xeXamChooseIndieGame(std::string* file_name, uint32_t* device_id,
                          std::string* display_name);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
