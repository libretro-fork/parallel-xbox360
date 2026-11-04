#include "core_atomic_float.h"
#include "core_shader_state.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
   retro_atomic_int_t volume;
   retro_atomic_int_t state;
   uint32_t bits = 0, result;
   unsigned i;
   float input, output;
   retro_atomic_int_init(&volume, 0);
   retro_atomic_int_init(&state, 0);
   if (xe_shader_ready(&state) || xe_shader_valid(&state)) abort();
   if (!xe_shader_claim(&state) || xe_shader_claim(&state)) abort();
   xe_shader_set_valid(&state, 1);
   if (xe_shader_ready(&state) || !xe_shader_valid(&state)) abort();
   xe_shader_publish(&state);
   xe_shader_set_valid(&state, 0);
   if (!xe_shader_ready(&state) || xe_shader_valid(&state) || xe_shader_claim(&state)) abort();
   for (i = 0; i < 1000000; ++i) {
      /* Finite IEEE values, including signed zeros and subnormals. */
      if ((bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000)) {
         memcpy(&input, &bits, sizeof(input));
         xe_atomic_float_store(&volume, input);
         output = xe_atomic_float_load(&volume);
         memcpy(&result, &output, sizeof(result));
         if (result != bits) abort();
      }
      bits = bits * UINT32_C(1664525) + UINT32_C(1013904223);
   }
   bits = UINT32_C(0x80000000);
   memcpy(&input, &bits, sizeof(input));
   xe_atomic_float_store(&volume, input);
   output = xe_atomic_float_load(&volume);
   memcpy(&result, &output, sizeof(result));
   if (result != bits) abort();
   puts("PASS: one million finite float-bit patterns without quantization");
   return 0;
}
