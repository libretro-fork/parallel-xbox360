/* Minimal C ABI bridge to the real C++ input driver for C89 tests. */
#include "libretro_hid.h"
#include "core_input_analog.h"
using xe::hid::libretro_hid::LibretroInputDriver;
extern "C" void* xe_test_input_new() { return new LibretroInputDriver(nullptr, 0); }
extern "C" void xe_test_input_free(void* driver) {
  delete static_cast<LibretroInputDriver*>(driver);
}
extern "C" void xe_test_input_update(void* driver, retro_input_state_t callback) {
  static_cast<LibretroInputDriver*>(driver)->UpdateFromLibretro(callback);
}
extern "C" unsigned xe_test_input_read(void* driver, xe_input_analog* out) {
  xe::hid::X_INPUT_STATE state;
  unsigned result = static_cast<LibretroInputDriver*>(driver)->GetState(0, &state);
  if (result) return result;
  out->left_trigger = state.gamepad.left_trigger;
  out->right_trigger = state.gamepad.right_trigger;
  out->left_x = state.gamepad.thumb_lx;
  out->left_y = state.gamepad.thumb_ly;
  out->right_x = state.gamepad.thumb_rx;
  out->right_y = state.gamepad.thumb_ry;
  return 0;
}
