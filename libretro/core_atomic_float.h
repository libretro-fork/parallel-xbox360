/* Preserve float bits without a C++ atomic<float>. MSVC C89-compatible. */
#ifndef XE_CORE_ATOMIC_FLOAT_H
#define XE_CORE_ATOMIC_FLOAT_H
#include <string.h>
#include <retro_atomic.h>
#include <retro_inline.h>
typedef char xe_atomic_float_size_check[sizeof(float) == sizeof(int) ? 1 : -1];
static INLINE void xe_atomic_float_store(retro_atomic_int_t *value, float input) {
   int bits;
   memcpy(&bits, &input, sizeof(bits));
   retro_atomic_store_release_int(value, bits);
}
static INLINE float xe_atomic_float_load(const retro_atomic_int_t *value) {
   int bits = retro_atomic_load_acquire_int(value);
   float result;
   memcpy(&result, &bits, sizeof(result));
   return result;
}
#endif
