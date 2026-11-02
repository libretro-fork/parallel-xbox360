/* Title-stop requests coalesce and do not leak into the next title. C89. */
#include "core_events.h"
int main(void) {
  xe_libretro_clear_title_stop();
  if (xe_libretro_take_title_stop()) return 1;
  xe_libretro_request_title_stop();
  xe_libretro_request_title_stop();
  if (!xe_libretro_take_title_stop() || xe_libretro_take_title_stop()) return 1;
  xe_libretro_request_title_stop();
  xe_libretro_clear_title_stop();
  return xe_libretro_take_title_stop() ? 1 : 0;
}
