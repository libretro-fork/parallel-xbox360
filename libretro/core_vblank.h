#ifndef XE_LIBRETRO_CORE_VBLANK_H
#define XE_LIBRETRO_CORE_VBLANK_H
#include <retro_common_api.h>
typedef struct xe_vblank_queue xe_vblank_queue;
RETRO_BEGIN_DECLS
xe_vblank_queue *xe_vblank_queue_new(void);
int xe_vblank_queue_request(xe_vblank_queue *queue);
int xe_vblank_queue_take(xe_vblank_queue *queue);
void xe_vblank_queue_stop(xe_vblank_queue *queue);
/* Destroy only after the interrupt worker has joined. */
void xe_vblank_queue_free(xe_vblank_queue *queue);
RETRO_END_DECLS
#endif