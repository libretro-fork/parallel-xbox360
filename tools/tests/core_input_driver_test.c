/* Exercise frontend polling through the real guest-facing driver; MSVC C89. */
#include <stdio.h>
#include "libretro.h"
#include "core_input_analog.h"
void *xe_test_input_new(void);
void xe_test_input_free(void *driver);
void xe_test_input_update(void *driver, retro_input_state_t callback);
unsigned xe_test_input_read(void *driver, xe_input_analog *out);
static int16_t axis, left_trigger, right_trigger;
static int digital;
static int16_t RETRO_CALLCONV input(unsigned port, unsigned device,
                                   unsigned index, unsigned id) {
  if (port) return 0;
  if (device == RETRO_DEVICE_JOYPAD)
    return (int16_t)(digital && (id == RETRO_DEVICE_ID_JOYPAD_L2 || id == RETRO_DEVICE_ID_JOYPAD_R2));
  if (device != RETRO_DEVICE_ANALOG) return 0;
  if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
    return id == RETRO_DEVICE_ID_JOYPAD_L2 ? left_trigger : right_trigger;
  return axis;
}
int main(void) {
  static const int16_t values[] = {-32768, -32767, -8688, -7848, -1, 0, 1, 7848, 8688, 32767};
  xe_input_analog state;
  void *driver = xe_test_input_new();
  unsigned i;
  int expected, level;
  if (!driver) return 1;
  for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    axis = values[i];
    expected = axis == -32768 ? 32767 : -axis;
    xe_test_input_update(driver, input);
    if (xe_test_input_read(driver, &state) || state.left_x != axis ||
        state.right_x != axis || state.left_y != expected || state.right_y != expected) return 2;
  }
  for (level = 0; level <= 255; ++level) {
    left_trigger = (int16_t)(level * 32767 / 255);
    right_trigger = (int16_t)((level * 32767 + 127) / 255);
    xe_test_input_update(driver, input);
    if (xe_test_input_read(driver, &state) || state.left_trigger != level ||
        state.right_trigger != level) return 3;
  }
  left_trigger = right_trigger = 0; digital = 1;
  xe_test_input_update(driver, input);
  if (xe_test_input_read(driver, &state) || state.left_trigger != 255 || state.right_trigger != 255) return 4;
  xe_test_input_free(driver);
  puts("PASS: actual input polling/guest state preserves analog precision and digital trigger fallback");
  return 0;
}
