#ifndef PARALLEL_XBOX360_CORE_EVENTS_H
#define PARALLEL_XBOX360_CORE_EVENTS_H
#ifdef __cplusplus
extern "C" {
#endif
void xe_libretro_request_title_stop(void);
int xe_libretro_take_title_stop(void);
void xe_libretro_clear_title_stop(void);
typedef struct xe_notice_view { const char *text; unsigned slot; } xe_notice_view;
/* Multiple producers, one frontend consumer. Coalesces recent messages in
 * fixed storage. Post copies borrowed strings once; acquire lends the result.
 * Up to 1023 UTF-8 bytes; busy writers/readers may force a post to be dropped. */
int xe_libretro_notice_post(const char *prefix, const char *text);
/* Release any previous lease before acquiring into the same view. */
int xe_libretro_notice_acquire(xe_notice_view *view);
/* Do not copy live leases or access their text after release. */
void xe_libretro_notice_release(xe_notice_view *view);
/* Quiesce producers and release all views before clearing. */
void xe_libretro_notice_clear(void);
#ifdef __cplusplus
}
#endif
#endif
