/* Preserve frontend analog precision until the guest's actual format boundary. */
#ifndef XE_CORE_INPUT_ANALOG_H
#define XE_CORE_INPUT_ANALOG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_input_analog {
  uint8_t left_trigger, right_trigger;
  int16_t left_x, left_y, right_x, right_y;
} xe_input_analog;
void xe_input_analog_map(xe_input_analog *out, int16_t left_trigger,
    int16_t right_trigger, int16_t left_x, int16_t left_y,
    int16_t right_x, int16_t right_y);
#ifdef __cplusplus
}
#endif
#endif
