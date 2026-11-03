/* Exhaustive analog mapping and original eight-bit pad levels; MSVC C89. */
#include <stdio.h>
#include "core_input_analog.h"
int main(void) {
  xe_input_analog out;
  int value, inverted, previous_y = 32767, previous_trigger = 0, error, level;
  for (value = -32768; value <= 32767; ++value) {
    xe_input_analog_map(&out, (int16_t)value, (int16_t)value,
        (int16_t)value, (int16_t)value, (int16_t)value, (int16_t)value);
    inverted = value == -32768 ? 32767 : -value;
    if (out.left_x != value || out.right_x != value ||
        out.left_y != inverted || out.right_y != inverted ||
        out.left_y > previous_y) return 1;
    previous_y = out.left_y;
    if (out.left_trigger != out.right_trigger || out.left_trigger < previous_trigger) return 2;
    previous_trigger = out.left_trigger;
    if (value < 0) {
      if (out.left_trigger) return 3;
    } else {
      error = (int)out.left_trigger * 32767 - value * 255;
      if (error < -16383 || error > 16383) return 4;
    }
  }
  for (level = 0; level <= 255; ++level) {
    /* Frontends may truncate or round when expanding a physical 8-bit pad. */
    xe_input_analog_map(&out, (int16_t)(level * 32767 / 255),
        (int16_t)((level * 32767 + 127) / 255), 1, -1, -1, 1);
    if (out.left_trigger != level || out.right_trigger != level ||
        out.left_x != 1 || out.left_y != 1 || out.right_x != -1 || out.right_y != -1) return 5;
  }
  puts("PASS: all analog endpoints/levels, raw stick precision and 256 trigger round trips");
  return 0;
}
