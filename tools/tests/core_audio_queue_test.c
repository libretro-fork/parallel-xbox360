/* Whole-frame SPSC ownership, wrap, readiness and cancellation; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "core_audio_queue.h"

static xe_audio_queue *queue;
static unsigned channels;
static retro_atomic_int_t started, done, failed;
static retro_eventcount_t completion;
static size_t written;

static float sample(size_t index) { return (float)(index % 16384) - 8192.0f; }

static void producer(void *opaque) {
  float data[511 * 6];
  size_t frame, count, i, result;
  (void)opaque;
  for (frame = 0; frame < 200000;) {
    count = 1 + frame % 511;
    if (count > 200000 - frame) count = 200000 - frame;
    for (i = 0; i < count * channels; ++i) data[i] = sample(frame * channels + i);
    result = xe_audio_queue_write(queue, data, count * channels);
    if (result != count * channels) {
      retro_atomic_store_release_int(&failed, 1); break;
    }
    frame += count;
  }
  retro_atomic_store_release_int(&done, 1);
  retro_eventcount_notify(&completion);
}

static void blocked_writer(void *opaque) {
  float frame[6] = {99, 99, 99, 99, 99, 99};
  (void)opaque;
  retro_atomic_store_release_int(&started, 1);
  written = xe_audio_queue_write(queue, frame, channels);
  retro_atomic_store_release_int(&done, 1);
  retro_eventcount_notify(&completion);
}

static int wait_done(void) {
  int key, attempt;
  for (attempt = 0; attempt < 4; ++attempt) {
    key = retro_eventcount_prepare_wait(&completion);
    if (retro_atomic_load_acquire_int(&done)) {
      retro_eventcount_cancel_wait(&completion); return 1;
    }
    if (!retro_eventcount_commit_wait_timeout(&completion, key, 2000000)) break;
  }
  return retro_atomic_load_acquire_int(&done);
}

static int stream_test(unsigned ch) {
  float mix[733 * 6 + 2];
  sthread_t *thread;
  size_t total = 0, count, got, i;
  channels = ch;
  retro_atomic_store_release_int(&done, 0);
  retro_atomic_store_release_int(&failed, 0);
  queue = xe_audio_queue_new(ch); if (!queue) return 0;
  if (xe_audio_queue_get_volume(queue) != 1.0f || xe_audio_queue_paused(queue)) return 0;
  xe_audio_queue_volume(queue, 0.25f); xe_audio_queue_pause(queue, 1);
  if (xe_audio_queue_get_volume(queue) != 0.25f || !xe_audio_queue_paused(queue)) return 0;
  xe_audio_queue_pause(queue, 0);
  if (xe_audio_queue_write(queue, mix, 1) || xe_audio_queue_mix(queue, mix, 1)) return 0;
  thread = sthread_create(producer, NULL); if (!thread) return 0;
  while (total < 200000 * channels) {
    count = 733 * channels;
    if (count > 200000 * channels - total) count = 200000 * channels - total;
    mix[0] = mix[count + 1] = 123.0f;
    for (i = 0; i < count; ++i) mix[i + 1] = 0.25f;
    got = xe_audio_queue_mix(queue, mix + 1, count);
    if (got % channels || mix[0] != 123.0f || mix[count + 1] != 123.0f) return 0;
    for (i = 0; i < got; ++i) if (mix[i + 1] != sample(total + i) + 0.25f) return 0;
    for (; i < count; ++i) if (mix[i + 1] != 0.25f) return 0;
    total += got;
    if (retro_atomic_load_acquire_int(&failed)) return 0;
    if (!got) sthread_yield();
  }
  sthread_join(thread);
  xe_audio_queue_stop(queue); xe_audio_queue_free(queue);
  return 1;
}

static int full_test(unsigned ch, int cancel) {
  float data[3072 * 6];
  sthread_t *thread;
  size_t count, i;
  int success;
  channels = ch;
  queue = xe_audio_queue_new(ch); if (!queue) return 0;
  count = 3072 * ch;
  for (i = 0; i < count; ++i) data[i] = sample(i);
  if (xe_audio_queue_write(queue, data, count) != count) return 0;
  retro_atomic_store_release_int(&started, 0);
  retro_atomic_store_release_int(&done, 0);
  thread = sthread_create(blocked_writer, NULL); if (!thread) return 0;
  while (!retro_atomic_load_acquire_int(&started)) sthread_yield();
  if (cancel) xe_audio_queue_stop(queue);
  else {
    memset(data, 0, ch * sizeof(float));
    if (xe_audio_queue_mix(queue, data, ch) != ch) return 0;
    for (i = 0; i < ch; ++i) if (data[i] != sample(i)) return 0;
  }
  success = wait_done();
  if (!success) xe_audio_queue_stop(queue);
  sthread_join(thread);
  if (!success || written != (cancel ? 0 : ch)) return 0;
  memset(data, 0, count * sizeof(float));
  if (xe_audio_queue_mix(queue, data, count) != count) return 0;
  for (i = 0; i < count - (cancel ? 0 : ch); ++i)
    if (data[i] != sample(i + (cancel ? 0 : ch))) return 0;
  if (!cancel) for (; i < count; ++i) if (data[i] != 99.0f) return 0;
  xe_audio_queue_stop(queue);
  if (xe_audio_queue_write(queue, data, ch)) return 0;
  xe_audio_queue_free(queue);
  return 1;
}

int main(void) {
  if (!retro_eventcount_init(&completion)) return 1;
  if (xe_audio_queue_new(1) || xe_audio_queue_new(8)) return 2;
  if (!stream_test(2) || !stream_test(6)) return 3;
  if (!full_test(2, 0) || !full_test(6, 0) ||
      !full_test(2, 1) || !full_test(6, 1)) return 4;
  retro_eventcount_free(&completion);
  puts("PASS: stereo/5.1 whole-frame wrap, 64ms bound, readiness and cancellation");
  return 0;
}
