/* C89 disc metadata/VFS implementation; minimal emulator-facing bridge. */
#ifndef LIBRETRO_DISC_H
#define LIBRETRO_DISC_H
#include "core_disc.h"
namespace disc {
inline void set_environment(retro_environment_t cb) { xe_disc_set_environment(cb); }
inline bool load(const char *path, char *start, size_t capacity) {
  return xe_disc_load(path, start, capacity) != 0;
}
inline void unload() { xe_disc_unload(); }
inline void deinit() { xe_disc_deinit(); }
inline void release() { xe_disc_release(); }
inline void resume() { xe_disc_resume(); }
inline void set_notice(xe_disc_notice_fn fn) { xe_disc_set_notice(fn); }
inline void request(uint8_t number, const char *message, xe_disc_request *node) {
  xe_disc_dispatch(number, message, node);
}
}
#endif
