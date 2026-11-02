/* In-process title-stop notification. MSVC C89-compatible. */
#ifndef PARALLEL_XBOX360_CORE_EVENTS_H
#define PARALLEL_XBOX360_CORE_EVENTS_H
#ifdef __cplusplus
extern "C" {
#endif
void xe_libretro_request_title_stop(void);
int xe_libretro_take_title_stop(void);
void xe_libretro_clear_title_stop(void);
#ifdef __cplusplus
}
#endif
#endif
