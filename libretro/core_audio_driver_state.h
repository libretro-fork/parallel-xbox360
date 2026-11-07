/* Quiescent native driver checkpoint. MSVC C89 metadata; no wire pointers. */
#ifndef XE_AUDIO_DRIVER_STATE_H
#define XE_AUDIO_DRIVER_STATE_H
#include "core_audio_queue.h"
#include <retro_inline.h>
typedef struct xe_audio_driver_view {
  uint32_t version, frequency, channels, output_channels, channel_frames, planar_be;
  double resample_position;
  float last[6];
  xe_audio_queue_view queue;
} xe_audio_driver_view;
static INLINE int xe_audio_driver_storage_external(const void *view,size_t bytes,
      const void *owner,size_t owner_bytes) {
  uintptr_t a=(uintptr_t)view,b=(uintptr_t)owner;
  if(!view || a>UINTPTR_MAX-bytes || b>UINTPTR_MAX-owner_bytes) return 0;
  return a>=b ? a-b>=owner_bytes : b-a>=bytes;
}
static INLINE int xe_audio_driver_view_valid(const xe_audio_driver_view *view,
      uint32_t frequency, uint32_t channels, uint32_t output_channels,
      uint32_t channel_frames, uint32_t planar_be) {
  double step;
  if (!view || view->version != 1 || !frequency ||
      view->frequency != frequency || view->channels != channels ||
      (channels != 2 && channels != 6) || view->output_channels != output_channels ||
      (output_channels != 2 && output_channels != 6) ||
      view->channel_frames != channel_frames || view->planar_be != planar_be ||
      planar_be > 1 || view->queue.state.channels != output_channels) return 0;
  step = (double)frequency / 48000.0;
  /* Ordered comparisons reject NaN/infinity without C99 math helpers. */
  return view->resample_position >= 0.0 &&
      (frequency == 48000 ? view->resample_position == 0.0 : view->resample_position < step);
}
#endif
