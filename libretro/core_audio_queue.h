/* One serialized producer and one frontend consumer; MSVC C89. */
#ifndef XE_CORE_AUDIO_QUEUE_H
#define XE_CORE_AUDIO_QUEUE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_audio_queue xe_audio_queue;
xe_audio_queue *xe_audio_queue_new(unsigned channels);
/* Stop wakes a blocked writer. Quiesce both sides before free. */
void xe_audio_queue_stop(xe_audio_queue *queue);
void xe_audio_queue_free(xe_audio_queue *queue);
/* Copies directly into queue spans, waiting for consumer readiness when full.
 * Count must contain complete channel frames. Invalid arguments return zero;
 * a valid write is short only after stop or for an unavailable queue. */
size_t xe_audio_queue_write(xe_audio_queue *queue, const float *data, size_t count);
/* Adds directly from borrowed queue spans into final mix storage. */
size_t xe_audio_queue_mix(xe_audio_queue *queue, float *mix, size_t count);
void xe_audio_queue_pause(xe_audio_queue *queue, int paused);
int xe_audio_queue_paused(const xe_audio_queue *queue);
void xe_audio_queue_volume(xe_audio_queue *queue, float volume);
float xe_audio_queue_get_volume(const xe_audio_queue *queue);
#ifdef __cplusplus
}
#endif
#endif
