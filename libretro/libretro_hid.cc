/*
 * Xenia Edge - Xbox 360 Emulator Libretro Core
 * Libretro HID (Input Driver) Implementation
 * Copyright (C) 2024 Xenia Edge Contributors
 */

#include "libretro_hid.h"
#include "core_input_analog.h"

#include <new>
#include <cstring>
#include <string>

#include "xenia/base/byte_order.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace hid {
namespace libretro_hid {

// Libretro RetroPad uses SNES positional layout:
//   B=south  A=east  Y=west  X=north
// Xbox 360:
//   A=south  B=east  X=west  Y=north
static uint16_t XboxButtons(const LibretroControllerState& s) {
  uint16_t btns = 0;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_UP])    btns |= X_INPUT_GAMEPAD_DPAD_UP;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_DOWN])  btns |= X_INPUT_GAMEPAD_DPAD_DOWN;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_LEFT])  btns |= X_INPUT_GAMEPAD_DPAD_LEFT;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_RIGHT]) btns |= X_INPUT_GAMEPAD_DPAD_RIGHT;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_START]) btns |= X_INPUT_GAMEPAD_START;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_SELECT])btns |= X_INPUT_GAMEPAD_BACK;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_L3])    btns |= X_INPUT_GAMEPAD_LEFT_THUMB;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_R3])    btns |= X_INPUT_GAMEPAD_RIGHT_THUMB;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_L])     btns |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_R])     btns |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_B])     btns |= X_INPUT_GAMEPAD_A;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_A])     btns |= X_INPUT_GAMEPAD_B;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_Y])     btns |= X_INPUT_GAMEPAD_X;
  if (s.buttons[RETRO_DEVICE_ID_JOYPAD_X])     btns |= X_INPUT_GAMEPAD_Y;
  return btns;
}

// The direction key for a stick, as XInput reports it: one of eight, past
// the stick's dead zone, or none (0). x right and y up positive.
static uint16_t StickKey(int x, int y, int deadzone, uint16_t up) {
  const bool u = y > deadzone, d = y < -deadzone;
  const bool r = x > deadzone, l = x < -deadzone;
  // Up, Down, Right, Left, UpLeft, UpRight, DownRight, DownLeft follow
  // each other from the Up key
  if (u && l) return up + 4;
  if (u && r) return up + 5;
  if (d && r) return up + 6;
  if (d && l) return up + 7;
  if (u) return up;
  if (d) return up + 1;
  if (r) return up + 2;
  if (l) return up + 3;
  return 0;
}

// ---- LibretroInputDriver --------------------------------------------------

LibretroInputDriver::LibretroInputDriver()
 {
  input_ = xe_input_mailbox_new();
  if (!input_) throw std::bad_alloc();
  // RetroArch starts with port 0 connected.
  for (size_t i = 0; i < kMaxPorts; ++i) {
    states_[i].connected = (i == 0);
    xe_input_packet packet = {};
    packet.connected = states_[i].connected;
    xe_input_packet_publish(input_, static_cast<unsigned>(i), &packet);
  }
}

LibretroInputDriver::~LibretroInputDriver() { xe_input_mailbox_free(input_); }

X_STATUS LibretroInputDriver::Setup() { return X_STATUS_SUCCESS; }

InputType LibretroInputDriver::GetInputType() const {
  return InputType::Controller;
}

std::vector<InputDeviceInfo> LibretroInputDriver::EnumerateDevices() {
  std::vector<InputDeviceInfo> devices;
  for (size_t i = 0; i < kMaxPorts; ++i) {
    InputDeviceInfo info;
    info.driver_slot = static_cast<uint8_t>(i);
    info.stable_id = "libretro:" + std::to_string(i);
    info.display_name = "RetroPad " + std::to_string(i + 1);
    info.preferred_slot = static_cast<int8_t>(i);
    devices.push_back(std::move(info));
  }
  return devices;
}

X_RESULT LibretroInputDriver::GetCapabilities(uint32_t user_index,
                                               uint32_t flags,
                                               X_INPUT_CAPABILITIES* out_caps) {
  if (user_index >= kMaxPorts) return X_ERROR_DEVICE_NOT_CONNECTED;

  if (!xe_input_connected(input_, user_index)) return X_ERROR_DEVICE_NOT_CONNECTED;

  std::memset(out_caps, 0, sizeof(*out_caps));
  out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  // be<T> auto byte-swaps - assign native values
  out_caps->flags = X_INPUT_CAPS_FFB_SUPPORTED;

  // Report full gamepad capability
  out_caps->gamepad.buttons = 0xFFFF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = 32767;
  out_caps->gamepad.thumb_ly = 32767;
  out_caps->gamepad.thumb_rx = 32767;
  out_caps->gamepad.thumb_ry = 32767;
  out_caps->vibration.left_motor_speed = 0xFFFF;
  out_caps->vibration.right_motor_speed = 0xFFFF;

  return X_ERROR_SUCCESS;
}

X_RESULT LibretroInputDriver::GetState(uint32_t user_index,
                                        X_INPUT_STATE* out_state) {
  if (user_index >= kMaxPorts) return X_ERROR_DEVICE_NOT_CONNECTED;

  xe_input_lease lease;
  const xe_input_packet* packet = xe_input_packet_acquire(input_, user_index, &lease);
  if (!packet) return X_ERROR_DEVICE_NOT_CONNECTED;
  if (!packet->connected) {
    xe_input_packet_release(&lease);
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  std::memset(out_state, 0, sizeof(*out_state));
  out_state->packet_number = packet->packet;
  out_state->gamepad.buttons = packet->buttons;
  out_state->gamepad.left_trigger = packet->analog.left_trigger;
  out_state->gamepad.right_trigger = packet->analog.right_trigger;
  out_state->gamepad.thumb_lx = packet->analog.left_x;
  out_state->gamepad.thumb_ly = packet->analog.left_y;
  out_state->gamepad.thumb_rx = packet->analog.right_x;
  out_state->gamepad.thumb_ry = packet->analog.right_y;
  xe_input_packet_release(&lease);
  return X_ERROR_SUCCESS;
}

X_RESULT LibretroInputDriver::SetState(uint32_t user_index,
                                        X_INPUT_VIBRATION* vibration) {
  if (user_index >= kMaxPorts) return X_ERROR_DEVICE_NOT_CONNECTED;

  if (!xe_input_connected(input_, user_index)) return X_ERROR_DEVICE_NOT_CONNECTED;

  if (vibration)
    xe_input_rumble_post(input_, user_index, vibration->left_motor_speed,
                        vibration->right_motor_speed);
  return X_ERROR_SUCCESS;
}

// XInputGetKeystroke for the RetroPad. Many titles drive their menus with it
// rather than with the state (Minecraft's main menu took no input at all while
// this said EMPTY), so the button, trigger and stick changes UpdateFromLibretro
// saw are handed out one at a time, oldest first.
X_RESULT LibretroInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                            X_INPUT_KEYSTROKE* out_keystroke) {
  if (user_index >= kMaxPorts) return X_ERROR_DEVICE_NOT_CONNECTED;

  if (!xe_input_connected(input_, user_index)) return X_ERROR_DEVICE_NOT_CONNECTED;

  xe_input_key key;
  if (!xe_input_key_take(input_, user_index, &key)) return X_ERROR_EMPTY;
  std::memset(out_keystroke, 0, sizeof(*out_keystroke));
  out_keystroke->virtual_key = key.key;
  out_keystroke->flags = key.flags;
  out_keystroke->user_index = static_cast<uint8_t>(user_index);
  return X_ERROR_SUCCESS;
}

// Turns the state just polled into keystrokes: a key down for each key that
// is now held and was not, a key up for each that was and is not, and, as
// XInput does, repeats for a held key - first after 400 ms, then every 100.
void LibretroInputDriver::UpdateKeystrokes(size_t port) {
  using xe::ui::VirtualKey;
  const auto& s = states_[port];
  if (!s.connected) {
    xe_input_keys_update(input_, static_cast<unsigned>(port), nullptr, 0, 0);
    return;
  }

  static const struct { uint16_t mask; VirtualKey key; } kButtons[] = {
      {X_INPUT_GAMEPAD_A, VirtualKey::kXInputPadA},
      {X_INPUT_GAMEPAD_B, VirtualKey::kXInputPadB},
      {X_INPUT_GAMEPAD_X, VirtualKey::kXInputPadX},
      {X_INPUT_GAMEPAD_Y, VirtualKey::kXInputPadY},
      {X_INPUT_GAMEPAD_RIGHT_SHOULDER, VirtualKey::kXInputPadRShoulder},
      {X_INPUT_GAMEPAD_LEFT_SHOULDER, VirtualKey::kXInputPadLShoulder},
      {X_INPUT_GAMEPAD_DPAD_UP, VirtualKey::kXInputPadDpadUp},
      {X_INPUT_GAMEPAD_DPAD_DOWN, VirtualKey::kXInputPadDpadDown},
      {X_INPUT_GAMEPAD_DPAD_LEFT, VirtualKey::kXInputPadDpadLeft},
      {X_INPUT_GAMEPAD_DPAD_RIGHT, VirtualKey::kXInputPadDpadRight},
      {X_INPUT_GAMEPAD_START, VirtualKey::kXInputPadStart},
      {X_INPUT_GAMEPAD_BACK, VirtualKey::kXInputPadBack},
      {X_INPUT_GAMEPAD_LEFT_THUMB, VirtualKey::kXInputPadLThumbPress},
      {X_INPUT_GAMEPAD_RIGHT_THUMB, VirtualKey::kXInputPadRThumbPress},
  };
  uint16_t now[18];
  size_t count = 0;
  const uint16_t btns = XboxButtons(s);
  for (const auto& b : kButtons)
    if (btns & b.mask) now[count++] = static_cast<uint16_t>(b.key);
  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD is 30 of 255
  if (s.left_trigger > 30 * 0x7FFF / 255)
    now[count++] = static_cast<uint16_t>(VirtualKey::kXInputPadLTrigger);
  if (s.right_trigger > 30 * 0x7FFF / 255)
    now[count++] = static_cast<uint16_t>(VirtualKey::kXInputPadRTrigger);
  // RetroArch's y is down positive, XInput's up positive
  if (uint16_t key = StickKey(s.left_stick_x, -s.left_stick_y,
                              X_INPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
                              static_cast<uint16_t>(VirtualKey::kXInputPadLThumbUp)))
    now[count++] = key;
  if (uint16_t key = StickKey(s.right_stick_x, -s.right_stick_y,
                              X_INPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,
                              static_cast<uint16_t>(VirtualKey::kXInputPadRThumbUp)))
    now[count++] = key;

  xe_input_keys_update(input_, static_cast<unsigned>(port), now, count, 1);
}

void LibretroInputDriver::UpdateFromLibretro(
    retro_input_state_t input_state_cb, double fps) {
  if (!input_state_cb) return;

  xe_input_begin_run(input_, fps);

  for (size_t port = 0; port < kMaxPorts; ++port) {
    auto& s = states_[port];

    // Read digital buttons
    bool any_input = false;
    for (int id = 0; id < 16; ++id) {
      s.buttons[id] = input_state_cb(
          static_cast<unsigned>(port), RETRO_DEVICE_JOYPAD, 0, id);
      if (s.buttons[id]) any_input = true;
    }

    // Read analog sticks
    s.left_stick_x = input_state_cb(
        static_cast<unsigned>(port), RETRO_DEVICE_ANALOG,
        RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
    s.left_stick_y = input_state_cb(
        static_cast<unsigned>(port), RETRO_DEVICE_ANALOG,
        RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
    s.right_stick_x = input_state_cb(
        static_cast<unsigned>(port), RETRO_DEVICE_ANALOG,
        RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
    s.right_stick_y = input_state_cb(
        static_cast<unsigned>(port), RETRO_DEVICE_ANALOG,
        RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

    if (s.left_stick_x || s.left_stick_y ||
        s.right_stick_x || s.right_stick_y)
      any_input = true;

    // Read analog triggers (L2/R2): try analog first, fallback to digital.
    int16_t lt = input_state_cb(
        static_cast<unsigned>(port), RETRO_DEVICE_ANALOG,
        RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L2);
    int16_t rt = input_state_cb(
        static_cast<unsigned>(port), RETRO_DEVICE_ANALOG,
        RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2);

    if (lt == 0 && s.buttons[RETRO_DEVICE_ID_JOYPAD_L2])
      lt = 0x7FFF;  // digital fallback
    if (rt == 0 && s.buttons[RETRO_DEVICE_ID_JOYPAD_R2])
      rt = 0x7FFF;

    s.left_trigger = lt;
    s.right_trigger = rt;
    if (lt || rt) any_input = true;

    // A port is connected if the frontend assigned a device to it.
    s.connected = port_connected_[port];

    if (any_input && s.connected) s.packet_number++;
    xe_input_packet packet = {};
    packet.packet = s.packet_number;
    packet.buttons = XboxButtons(s);
    packet.connected = s.connected;
    xe_input_analog_map(&packet.analog, s.left_trigger, s.right_trigger,
        s.left_stick_x, s.left_stick_y, s.right_stick_x, s.right_stick_y);
    if (xe_input_packet_publish(input_, static_cast<unsigned>(port), &packet))
      UpdateKeystrokes(port);
    uint16_t strong, weak;
    if (xe_input_rumble_take(input_, static_cast<unsigned>(port), &strong, &weak) && rumble_cb_) {
      if (!s.connected) strong = weak = 0;
      rumble_cb_(static_cast<unsigned>(port), RETRO_RUMBLE_STRONG, strong);
      rumble_cb_(static_cast<unsigned>(port), RETRO_RUMBLE_WEAK, weak);
    }
  }
}

void LibretroInputDriver::SetRumbleCallback(retro_set_rumble_state_t cb) {
  rumble_cb_ = cb;
}

void LibretroInputDriver::SetPortDevice(unsigned port, unsigned device) {
  if (port >= kMaxPorts) return;
  // RETRO_DEVICE_NONE (0) = disconnected, other = connected.
  port_connected_[port] = (device != RETRO_DEVICE_NONE);
  states_[port].connected = port_connected_[port];
  if (!port_connected_[port]) {
    // Clear state for disconnected port
    states_[port] = {};
  }
  xe_input_packet packet = {};
  const auto& state = states_[port];
  packet.packet = state.packet_number;
  packet.buttons = XboxButtons(state);
  xe_input_analog_map(&packet.analog, state.left_trigger, state.right_trigger,
      state.left_stick_x, state.left_stick_y, state.right_stick_x, state.right_stick_y);
  packet.connected = port_connected_[port];
  xe_input_packet_publish(input_, port, &packet);
  if (!packet.connected) {
    xe_input_keys_update(input_, port, nullptr, 0, 0);
    xe_input_rumble_post(input_, port, 0, 0);
  }
}

// ---- Factory --------------------------------------------------------------

std::unique_ptr<InputDriver> Create() {
  return std::make_unique<LibretroInputDriver>();
}

}  // namespace libretro_hid
}  // namespace hid
}  // namespace xe
