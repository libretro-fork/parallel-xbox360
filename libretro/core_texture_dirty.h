/* Atomic texture-data hints. All updates after publication require the existing
 * shared-memory global critical region; readers need no lock. No new lock or
 * atomic RMW is required. This does not protect texture lifetime/watch handles.
 * MSVC C89. */
#ifndef XE_CORE_TEXTURE_DIRTY_H
#define XE_CORE_TEXTURE_DIRTY_H
#include <retro_atomic.h>
#include <retro_inline.h>
enum {XE_TEXTURE_DIRTY_BASE=1, XE_TEXTURE_DIRTY_MIPS=2, XE_TEXTURE_DIRTY_ALL=3};
typedef struct xe_texture_dirty {retro_atomic_int_t bits;} xe_texture_dirty;
static INLINE void xe_texture_dirty_init(xe_texture_dirty *state, unsigned mask) {
  retro_atomic_int_init(&state->bits,(int)(mask & XE_TEXTURE_DIRTY_ALL));
}
static INLINE unsigned xe_texture_dirty_load(const xe_texture_dirty *state) {
  return (unsigned)retro_atomic_load_acquire_int(&state->bits);
}
static INLINE unsigned xe_texture_dirty_load_locked(const xe_texture_dirty *state) {
  return (unsigned)retro_atomic_load_relaxed_int(&state->bits);
}
/* Writers are externally serialized, so a release store preserves the other
 * level's flag without a locked read-modify-write instruction. */
static INLINE void xe_texture_dirty_update_locked(xe_texture_dirty *state,
    unsigned set, unsigned clear) {
  unsigned mask=xe_texture_dirty_load_locked(state);
  mask=((mask | set) & ~clear) & XE_TEXTURE_DIRTY_ALL;
  retro_atomic_store_release_int(&state->bits,(int)mask);
}
#endif
