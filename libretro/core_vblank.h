#ifndef XE_LIBRETRO_CORE_VBLANK_H
#define XE_LIBRETRO_CORE_VBLANK_H
#include <retro_common_api.h>
#include <stdint.h>
typedef struct xe_vblank_queue xe_vblank_queue;
RETRO_BEGIN_DECLS
xe_vblank_queue *xe_vblank_queue_new(void);
int xe_vblank_queue_request(xe_vblank_queue *queue);
int xe_vblank_queue_take(xe_vblank_queue *queue);
/* One controller; acknowledges only between interrupt callbacks. Failure
 * resumes only a pause acquired by this call. Pending credits are retained. */
int xe_vblank_queue_pause(xe_vblank_queue *queue,int64_t timeout_us);
int xe_vblank_queue_is_paused(const xe_vblank_queue *queue);
void xe_vblank_queue_resume(xe_vblank_queue *queue);
void xe_vblank_queue_stop(xe_vblank_queue *queue);
/* Destroy only after the interrupt worker has joined. */
void xe_vblank_queue_free(xe_vblank_queue *queue);
RETRO_END_DECLS
#endif