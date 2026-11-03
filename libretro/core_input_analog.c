#include "core_input_analog.h"

static uint8_t trigger(int16_t value) {
  if (value <= 0) return 0;
  /* Xbox triggers have eight bits. Round once at that required boundary. */
  return (uint8_t)(((int)value * 255 + 16383) / 32767);
}
static int16_t invert_y(int16_t value) {
  /* Positive 32768 cannot be represented; saturate instead of wrapping. */
  return value == -32768 ? 32767 : (int16_t)-(int)value;
}
void xe_input_analog_map(xe_input_analog *out, int16_t left_trigger,
    int16_t right_trigger, int16_t left_x, int16_t left_y,
    int16_t right_x, int16_t right_y) {
  out->left_trigger = trigger(left_trigger);
  out->right_trigger = trigger(right_trigger);
  /* Dead-zone policy belongs to the frontend and game. XInput state is raw. */
  out->left_x = left_x;
  out->left_y = invert_y(left_y);
  out->right_x = right_x;
  out->right_y = invert_y(right_y);
}
