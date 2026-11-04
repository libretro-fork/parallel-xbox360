/* Minimal C ABI for exercising the actual C++ libretro input adapter. */
#include "libretro_hid.h"
#include <cstdint>
using xe::hid::libretro_hid::LibretroInputDriver;
static LibretroInputDriver* reentrant_driver;
static bool reentered, reentry_failed;
static int16_t RETRO_CALLCONV poll_input(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (port) return 0;
  if (device == RETRO_DEVICE_JOYPAD) return id == RETRO_DEVICE_ID_JOYPAD_B;
  if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
    return id == RETRO_DEVICE_ID_ANALOG_X ? 12345 : -23456;
  return 0;
}
static int16_t RETRO_CALLCONV reentry_poll_input(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (reentrant_driver && !reentered) {
    reentered = true;
    xe::hid::X_INPUT_STATE state;
    reentry_failed = reentrant_driver->GetState(0, &state) != 0;
  }
  return poll_input(port, device, index, id);
}
extern "C" void* input_fixture_new(void) { return new LibretroInputDriver(); }
extern "C" void input_fixture_free(void* input) { delete static_cast<LibretroInputDriver*>(input); }
extern "C" void input_fixture_poll(void* input) {
#ifdef INPUT_OLD
  static_cast<LibretroInputDriver*>(input)->UpdateFromLibretro(poll_input);
#else
  static_cast<LibretroInputDriver*>(input)->UpdateFromLibretro(poll_input, 60.0);
#endif
}
extern "C" uint32_t input_fixture_read(void* input) {
  xe::hid::X_INPUT_STATE state;
  auto result = static_cast<LibretroInputDriver*>(input)->GetState(0, &state);
  if (result != 0 || state.gamepad.buttons != xe::hid::X_INPUT_GAMEPAD_A ||
      state.gamepad.thumb_lx != 12345 || state.gamepad.thumb_ly != 23456) return 0;
  return uint32_t(state.packet_number);
}

static unsigned rumble_calls;
static uint16_t rumble_values[2];
static bool RETRO_CALLCONV record_rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
  if (port != 0 || effect > 1) return false;
  ++rumble_calls; rumble_values[effect] = strength; return true;
}
extern "C" int input_fixture_validate(void) {
  LibretroInputDriver driver;
  xe::hid::X_INPUT_STATE state;
  xe::hid::X_INPUT_VIBRATION vibration;
  xe::hid::X_INPUT_KEYSTROKE key;
  auto poll = [&driver]() {
    retro_input_state_t callback = reentrant_driver ? reentry_poll_input : poll_input;
#ifdef INPUT_OLD
    driver.UpdateFromLibretro(callback);
#else
    driver.UpdateFromLibretro(callback, 60.0);
#endif
  };
  rumble_calls = 0;
  driver.SetRumbleCallback(record_rumble);
#ifndef INPUT_OLD
  reentrant_driver = &driver; reentered = reentry_failed = false;
#endif
  poll();
#ifndef INPUT_OLD
  reentrant_driver = nullptr;
  if (!reentered || reentry_failed) return 0;
#endif
  if (driver.GetState(1, &state) == 0 || driver.GetState(0, &state) != 0) return 0;
  vibration.left_motor_speed = 123; vibration.right_motor_speed = 456;
  if (driver.SetState(0, &vibration) != 0) return 0;
#ifdef INPUT_OLD
  if (rumble_calls != 2) return 0;
#else
  if (rumble_calls != 0) return 0;
#endif
  poll();
  if (rumble_calls != 2 || rumble_values[RETRO_RUMBLE_STRONG] != 123 ||
      rumble_values[RETRO_RUMBLE_WEAK] != 456) return 0;
  if (driver.GetState(0, &state) != 0) return 0;
  uint32_t prior_packet = state.packet_number;
  driver.SetPortDevice(0, RETRO_DEVICE_JOYPAD);
  if (driver.GetState(0, &state) != 0 || state.packet_number != prior_packet ||
      state.gamepad.buttons != xe::hid::X_INPUT_GAMEPAD_A ||
      state.gamepad.thumb_lx != 12345) return 0;
  if (driver.GetKeystroke(0, 0, &key) != 0 || key.virtual_key != 0x5800 || key.flags != 1) return 0;
  driver.SetPortDevice(0, RETRO_DEVICE_NONE);
  if (driver.GetState(0, &state) == 0 || driver.GetKeystroke(0, 0, &key) == 0) return 0;
  poll();
#ifndef INPUT_OLD
  if (rumble_calls != 4 || rumble_values[0] || rumble_values[1]) return 0;
#endif
  driver.SetPortDevice(1, RETRO_DEVICE_JOYPAD); poll();
  return driver.GetState(1, &state) == 0 && driver.EnumerateDevices().size() == 4;
}
