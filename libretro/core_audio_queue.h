/* One serialized producer and one frontend consumer; MSVC C89. */
#ifndef XE_CORE_AUDIO_QUEUE_H
#define XE_CORE_AUDIO_QUEUE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_audio_queue xe_audio_queue;
typedef struct xe_audio_registry xe_audio_registry;
typedef struct xe_audio_group xe_audio_group;
/* A single frontend mixer and concurrent driver creation/retirement.
 * Stop is thread-safe; quiesce creation, retirement and the frontend before
 * releasing its root owner. Existing queues may be freed afterwards.
 * Registered queues keep the registry alive until their final retirement. */
xe_audio_registry *xe_audio_registry_new(unsigned channels);
void xe_audio_registry_stop(xe_audio_registry *registry);
void xe_audio_registry_free(xe_audio_registry *registry);
xe_audio_queue *xe_audio_registry_queue_new(xe_audio_registry *registry);
size_t xe_audio_registry_mix(xe_audio_registry *registry, float *mix, size_t count);
/* A subsystem generation may stop/release its group while the content-owned
 * registry is being mixed. Quiesce group creation before releasing its owner;
 * its existing queues retain the group until their retirement. */
xe_audio_group *xe_audio_group_new(xe_audio_registry *registry);
void xe_audio_group_stop(xe_audio_group *group);
void xe_audio_group_free(xe_audio_group *group);
xe_audio_queue *xe_audio_group_queue_new(xe_audio_group *group);
xe_audio_queue *xe_audio_queue_new(unsigned channels);
/* Stop wakes a blocked writer. Quiesce the producer before free; a registered
 * queue defers destruction until any frontend read lease has ended and a cold
 * registry creation/destruction call collects it. Standalone
 * queues require both sides quiesced before free. */
void xe_audio_queue_stop(xe_audio_queue *queue);
void xe_audio_queue_free(xe_audio_queue *queue);
/* Copies directly into queue spans, waiting for consumer readiness when full.
 * Count must contain complete channel frames. Invalid arguments return zero;
 * a valid write is short only after stop or for an unavailable queue. */
size_t xe_audio_queue_write(xe_audio_queue *queue, const float *data, size_t count);
/* Convert directly into owned queue spans, preserving complete-frame publication.
 * Input is borrowed until return. No resampling; count is input frames.
 * sequential_be requires six planar big-endian channels. Returns output sample
 * count, like write(), rather than frames. Stop may short-write. */
size_t xe_audio_queue_write_converted(xe_audio_queue *queue, const float *data,
      size_t frames, unsigned channels, int sequential_be, float volume);
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
