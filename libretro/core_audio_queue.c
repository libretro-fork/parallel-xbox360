#include "core_audio_queue.h"
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <retro_spsc.h>
#include <rthreads/retro_eventcount.h>

typedef char xe_audio_float_bits_fit_int[(sizeof(float) == sizeof(int)) ? 1 : -1];

struct xe_audio_queue {
  retro_spsc_t samples;
  retro_eventcount_t space;
  retro_atomic_int_t stopped, paused, volume;
  size_t limit, frame_bytes;
};

xe_audio_queue *xe_audio_queue_new(unsigned channels) {
  xe_audio_queue *queue;
  if (channels != 2 && channels != 6) return NULL;
  queue = (xe_audio_queue *)calloc(1, sizeof(*queue));
  if (!queue) return NULL;
  /* Physical storage may round up; the logical bound is exactly 64 ms. */
  queue->limit = (48000 * channels * 64 / 1000) * sizeof(float);
  queue->frame_bytes = channels * sizeof(float);
#ifdef XE_AUDIO_QUEUE_PLAIN
  if (!retro_spsc_init(&queue->samples, queue->limit)) {
#else
  if (!retro_spsc_init_mirrored(&queue->samples, queue->limit)) {
#endif
    free(queue); return NULL;
  }
  if (!retro_eventcount_init(&queue->space)) {
    retro_spsc_free(&queue->samples); free(queue); return NULL;
  }
  xe_audio_queue_volume(queue, 1.0f);
  return queue;
}

void xe_audio_queue_stop(xe_audio_queue *queue) {
  if (!queue) return;
  retro_atomic_store_release_int(&queue->stopped, 1);
  retro_eventcount_notify(&queue->space);
}

void xe_audio_queue_free(xe_audio_queue *queue) {
  if (!queue) return;
  retro_eventcount_free(&queue->space);
  retro_spsc_free(&queue->samples);
  free(queue);
}

static size_t room(const xe_audio_queue *queue) {
  size_t used = queue->samples.capacity - retro_spsc_write_avail(&queue->samples);
  return used < queue->limit ? queue->limit - used : 0;
}

size_t xe_audio_queue_write(xe_audio_queue *queue, const float *data, size_t count) {
  size_t written = 0, span, available, remaining;
  void *destination;
  int key;
  if (!queue || (!data && count) || count % (queue->frame_bytes / sizeof(float))) return 0;
  while (written < count && !retro_atomic_load_acquire_int(&queue->stopped)) {
    available = room(queue);
    if (!available) {
      key = retro_eventcount_prepare_wait(&queue->space);
      if (room(queue) || retro_atomic_load_acquire_int(&queue->stopped))
        retro_eventcount_cancel_wait(&queue->space);
      else {
        /* Safety recheck only; consumption/stop notifications drive readiness.
         * This timeout never advances guest time or schedules audio samples. */
        retro_eventcount_commit_wait_timeout(&queue->space, key, 1000000);
      }
      continue;
    }
    span = retro_spsc_write_begin(&queue->samples, &destination);
    if (span > available) span = available;
    span -= span % queue->frame_bytes;
    if (!span) {
      /* A plain ring can wrap in the middle of a six-channel frame. Publish
       * that complete frame in one operation, never a partial channel group. */
      retro_spsc_write_end(&queue->samples, 0);
      retro_spsc_write(&queue->samples, data + written, queue->frame_bytes);
      written += queue->frame_bytes / sizeof(float);
      continue;
    }
    remaining = count - written;
    if (span / sizeof(float) > remaining) span = remaining * sizeof(float);
    memcpy(destination, data + written, span);
    retro_spsc_write_end(&queue->samples, span);
    written += span / sizeof(float);
  }
  return written;
}

size_t xe_audio_queue_mix(xe_audio_queue *queue, float *mix, size_t count) {
  const void *source;
  const float *samples;
  float wrapped[6];
  size_t total = 0, span, i;
  if (!queue || (!mix && count) || count % (queue->frame_bytes / sizeof(float))) return 0;
  while (total < count) {
    span = retro_spsc_read_begin(&queue->samples, &source) / sizeof(float);
    if (!span) break;
    if (span < queue->frame_bytes / sizeof(float)) {
      /* Required only for a physical wrap on non-mirrored backends. */
      retro_spsc_read_end(&queue->samples, 0);
      retro_spsc_peek(&queue->samples, wrapped, queue->frame_bytes);
      span = queue->frame_bytes / sizeof(float);
      for (i = 0; i < span; ++i) mix[total + i] += wrapped[i];
      retro_spsc_skip(&queue->samples, queue->frame_bytes);
      total += span;
      retro_eventcount_notify(&queue->space);
      continue;
    }
    span -= span % (queue->frame_bytes / sizeof(float));
    if (span > count - total) span = count - total;
    samples = (const float *)source;
    for (i = 0; i < span; ++i) mix[total + i] += samples[i];
    retro_spsc_read_end(&queue->samples, span * sizeof(float));
    total += span;
    retro_eventcount_notify(&queue->space);
  }
  return total;
}

void xe_audio_queue_pause(xe_audio_queue *queue, int paused) {
  if (queue) retro_atomic_store_release_int(&queue->paused, paused != 0);
}
int xe_audio_queue_paused(const xe_audio_queue *queue) {
  return queue && retro_atomic_load_acquire_int(&queue->paused);
}
void xe_audio_queue_volume(xe_audio_queue *queue, float volume) {
  int bits;
  if (!queue) return;
  memcpy(&bits, &volume, sizeof(bits));
  retro_atomic_store_release_int(&queue->volume, bits);
}
float xe_audio_queue_get_volume(const xe_audio_queue *queue) {
  int bits;
  float volume;
  if (!queue) return 1.0f;
  bits = retro_atomic_load_acquire_int(&queue->volume);
  memcpy(&volume, &bits, sizeof(volume));
  return volume;
}
