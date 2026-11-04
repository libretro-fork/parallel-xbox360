/* One-word shader claim/validity/publication state. MSVC C89-compatible. */
#ifndef XE_CORE_SHADER_STATE_H
#define XE_CORE_SHADER_STATE_H
#include <retro_atomic.h>
#include <retro_inline.h>
#define XE_SHADER_VALID 1
#define XE_SHADER_READY 2
#define XE_SHADER_CLAIMED 4
static INLINE int xe_shader_claim(retro_atomic_int_t *state) {
   return !(retro_atomic_fetch_or_int(state, XE_SHADER_CLAIMED) & XE_SHADER_CLAIMED);
}
static INLINE int xe_shader_valid(const retro_atomic_int_t *state) {
   return (retro_atomic_load_acquire_int(state) & XE_SHADER_VALID) != 0;
}
static INLINE int xe_shader_ready(const retro_atomic_int_t *state) {
   return (retro_atomic_load_acquire_int(state) & XE_SHADER_READY) != 0;
}
static INLINE void xe_shader_set_valid(retro_atomic_int_t *state, int valid) {
   if (valid) retro_atomic_fetch_or_int(state, XE_SHADER_VALID);
   else retro_atomic_fetch_and_int(state, ~XE_SHADER_VALID);
}
/* Only after binary, backend resources and errors have been fully prepared. */
static INLINE void xe_shader_publish(retro_atomic_int_t *state) {
   retro_atomic_fetch_or_int(state, XE_SHADER_READY);
}
#endif
