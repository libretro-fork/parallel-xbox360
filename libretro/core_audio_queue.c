#include "core_audio_queue.h"
#include "core_audio_convert.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <retro_spsc.h>
#include <rthreads/retro_eventcount.h>

typedef char xe_audio_float_bits_fit_int[(sizeof(float) == sizeof(int)) ? 1 : -1];

#define AUDIO_SLOTS 16
#define SLOT_ACTIVE 1
#define SLOT_READING 2
#define SLOT_RETIRED 4
#define SLOT_BUILDING 8
#define SLOT_OWNER_RELEASING 16
#ifdef XE_AUDIO_REGISTRY_TEST
void xe_audio_registry_test_lease(xe_audio_queue *queue);
#endif
struct xe_audio_slot {
  retro_atomic_int_t state;
  xe_audio_queue *queue;
};
struct xe_audio_registry {
  struct xe_audio_slot slots[AUDIO_SLOTS];
  retro_eventcount_t space;
  retro_atomic_int_t stopped, references;
  unsigned channels;
};
struct xe_audio_group {
  xe_audio_registry *registry;
  retro_atomic_int_t stopped, references;
};
static void group_release(xe_audio_group *group);

struct xe_audio_queue {
  retro_spsc_t samples;
  retro_eventcount_t space;
  retro_atomic_int_t stopped, paused, volume;
  size_t limit, frame_bytes;
  xe_audio_registry *registry;
  xe_audio_group *group;
  unsigned slot;
  int local_space_initialized;
};

static xe_audio_queue *queue_allocate(unsigned channels, int local_space) {
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
  if (local_space && !retro_eventcount_init(&queue->space)) {
    retro_spsc_free(&queue->samples); free(queue); return NULL;
  }
  queue->local_space_initialized = local_space;
  xe_audio_queue_volume(queue, 1.0f);
  return queue;
}

xe_audio_queue *xe_audio_queue_new(unsigned channels) { return queue_allocate(channels, 1); }

static retro_eventcount_t *space_event(xe_audio_queue *queue) {
  return queue->registry ? &queue->registry->space : &queue->space;
}
static int stopped(const xe_audio_queue *queue) {
  return retro_atomic_load_acquire_int(&queue->stopped) ||
      (queue->registry && retro_atomic_load_acquire_int(&queue->registry->stopped)) ||
      (queue->group && retro_atomic_load_acquire_int(&queue->group->stopped));
}

void xe_audio_queue_stop(xe_audio_queue *queue) {
  if (!queue) return;
  retro_atomic_store_release_int(&queue->stopped, 1);
  retro_eventcount_notify(space_event(queue));
}

static void queue_destroy(xe_audio_queue *queue) {
  xe_audio_group *group;
  if (!queue) return;
  group = queue->group;
  if (queue->local_space_initialized) retro_eventcount_free(&queue->space);
  retro_spsc_free(&queue->samples);
  free(queue);
  if (group) group_release(group);
}

static void registry_release(xe_audio_registry *registry) {
  if (retro_atomic_fetch_sub_int(&registry->references, 1) != 1) return;
  retro_eventcount_free(&registry->space);
  free(registry);
}
static void group_release(xe_audio_group *group) {
  xe_audio_registry *registry;
  if (retro_atomic_fetch_sub_int(&group->references, 1) != 1) return;
  registry = group->registry;
  free(group);
  registry_release(registry);
}
static void retire(xe_audio_queue *queue) {
  xe_audio_registry *registry = queue->registry;
  struct xe_audio_slot *slot = &registry->slots[queue->slot];
  slot->queue = NULL;
  retro_atomic_store_release_int(&slot->state, 0);
  queue_destroy(queue);
  registry_release(registry);
}
static void collect_retired(xe_audio_registry *registry) {
  unsigned i;
  struct xe_audio_slot *slot;
  for (i = 0; i < AUDIO_SLOTS; ++i) {
    slot = &registry->slots[i];
    if (retro_atomic_load_relaxed_int(&slot->state) != (SLOT_ACTIVE | SLOT_RETIRED)) continue;
    if (retro_atomic_cas_int(&slot->state, SLOT_ACTIVE | SLOT_RETIRED, SLOT_BUILDING))
      retire(slot->queue);
  }
}
void xe_audio_queue_free(xe_audio_queue *queue) {
  struct xe_audio_slot *slot;
  if (!queue) return;
  if (!queue->registry) { queue_destroy(queue); return; }
  slot = &queue->registry->slots[queue->slot];
  /* Keep collectors away until this owner has stopped touching the queue.
   * Without OWNER_RELEASING a collector could free/reuse the slot between
   * retirement publication and our attempt to claim immediate cleanup. */
  retro_atomic_fetch_or_int(&slot->state, SLOT_RETIRED | SLOT_OWNER_RELEASING);
  if (retro_atomic_cas_int(&slot->state,
      SLOT_ACTIVE | SLOT_RETIRED | SLOT_OWNER_RELEASING, SLOT_BUILDING))
    retire(queue);
  else
    retro_atomic_fetch_and_int(&slot->state, ~SLOT_OWNER_RELEASING);
  /* No queue or slot access after publishing the end of owner release. */
}

xe_audio_registry *xe_audio_registry_new(unsigned channels) {
  xe_audio_registry *registry;
  if (channels != 2 && channels != 6) return NULL;
  registry = (xe_audio_registry *)calloc(1, sizeof(*registry));
  if (!registry) return NULL;
  if (!retro_eventcount_init(&registry->space)) { free(registry); return NULL; }
  registry->channels = channels;
  retro_atomic_store_release_int(&registry->references, 1);
  return registry;
}
void xe_audio_registry_stop(xe_audio_registry *registry) {
  if (!registry) return;
  retro_atomic_store_release_int(&registry->stopped, 1);
  /* No stream pointer traversal, including when a stream is being retired. */
  retro_eventcount_notify(&registry->space);
}
void xe_audio_registry_free(xe_audio_registry *registry) {
  if (!registry) return;
  xe_audio_registry_stop(registry);
  collect_retired(registry);
  registry_release(registry);
}
static xe_audio_queue *registered_queue_new(xe_audio_registry *registry, xe_audio_group *group) {
  xe_audio_queue *queue;
  unsigned i;
  if (!registry || retro_atomic_load_acquire_int(&registry->stopped) ||
      (group && retro_atomic_load_acquire_int(&group->stopped))) return NULL;
  collect_retired(registry);
  queue = queue_allocate(registry->channels, 0);
  if (!queue) return NULL;
  for (i = 0; i < AUDIO_SLOTS; ++i) {
    if (!retro_atomic_cas_int(&registry->slots[i].state, 0, SLOT_BUILDING)) continue;
    if (retro_atomic_load_acquire_int(&registry->stopped) ||
        (group && retro_atomic_load_acquire_int(&group->stopped))) {
      retro_atomic_store_release_int(&registry->slots[i].state, 0);
      queue_destroy(queue); return NULL;
    }
    queue->registry = registry;
    queue->group = group;
    if (group) retro_atomic_fetch_add_int(&group->references, 1);
    queue->slot = i;
    retro_atomic_fetch_add_int(&registry->references, 1);
    registry->slots[i].queue = queue;
    retro_atomic_store_release_int(&registry->slots[i].state, SLOT_ACTIVE);
    return queue;
  }
  queue_destroy(queue);
  return NULL;
}
xe_audio_queue *xe_audio_registry_queue_new(xe_audio_registry *registry) {
  return registered_queue_new(registry, NULL);
}
xe_audio_group *xe_audio_group_new(xe_audio_registry *registry) {
  xe_audio_group *group;
  if (!registry || retro_atomic_load_acquire_int(&registry->stopped)) return NULL;
  group = (xe_audio_group *)calloc(1, sizeof(*group));
  if (!group) return NULL;
  group->registry = registry;
  retro_atomic_store_release_int(&group->references, 1);
  retro_atomic_fetch_add_int(&registry->references, 1);
  return group;
}
void xe_audio_group_stop(xe_audio_group *group) {
  if (!group) return;
  retro_atomic_store_release_int(&group->stopped, 1);
  retro_eventcount_notify(&group->registry->space);
}
void xe_audio_group_free(xe_audio_group *group) {
  if (!group) return;
  xe_audio_group_stop(group);
  collect_retired(group->registry);
  group_release(group);
}
xe_audio_queue *xe_audio_group_queue_new(xe_audio_group *group) {
  return group ? registered_queue_new(group->registry, group) : NULL;
}

static size_t room(const xe_audio_queue *queue) {
  size_t used = queue->samples.capacity - retro_spsc_write_avail(&queue->samples);
  return used < queue->limit ? queue->limit - used : 0;
}

int xe_audio_queue_can_write_frames(const xe_audio_queue *queue, size_t frames) {
  if (!queue || stopped(queue) || frames > SIZE_MAX / queue->frame_bytes) return 0;
  return frames * queue->frame_bytes <= room(queue);
}

typedef struct xe_audio_convert_source {
  size_t frames;
  unsigned channels;
  int sequential_be;
  float volume;
} xe_audio_convert_source;

static size_t write_samples(xe_audio_queue *queue, const float *data, size_t count,
      const xe_audio_convert_source *convert) {
  size_t written = 0, span, available, remaining;
  void *destination;
  float wrapped[6];
  unsigned output_channels;
  int key;
  if (!queue || (!data && count) || count % (queue->frame_bytes / sizeof(float))) return 0;
  output_channels = (unsigned)(queue->frame_bytes / sizeof(float));
  while (written < count && !stopped(queue)) {
    available = room(queue);
    if (!available) {
      key = retro_eventcount_prepare_wait(space_event(queue));
      if (room(queue) || stopped(queue))
        retro_eventcount_cancel_wait(space_event(queue));
      else {
        retro_eventcount_commit_wait(space_event(queue), key);
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
      if (convert) {
        xe_audio_convert(wrapped, data, convert->frames, written / output_channels,
              1, convert->channels, output_channels, convert->sequential_be,
              convert->volume);
        retro_spsc_write(&queue->samples, wrapped, queue->frame_bytes);
      } else retro_spsc_write(&queue->samples, data + written, queue->frame_bytes);
      written += queue->frame_bytes / sizeof(float);
      continue;
    }
    remaining = count - written;
    if (span / sizeof(float) > remaining) span = remaining * sizeof(float);
    if (convert)
      xe_audio_convert((float *)destination, data, convert->frames,
            written / output_channels, span / queue->frame_bytes,
            convert->channels, output_channels, convert->sequential_be,
            convert->volume);
    else memcpy(destination, data + written, span);
    retro_spsc_write_end(&queue->samples, span);
    written += span / sizeof(float);
  }
  return written;
}

size_t xe_audio_queue_write(xe_audio_queue *queue, const float *data, size_t count) {
  return write_samples(queue, data, count, NULL);
}

size_t xe_audio_queue_write_converted(xe_audio_queue *queue, const float *data,
      size_t frames, unsigned channels, int sequential_be, float volume) {
  xe_audio_convert_source source;
  size_t output_channels;
  if (!queue || (!data && frames) || (channels != 2 && channels != 6) ||
      (sequential_be && channels != 6) || frames > SIZE_MAX / (6 * sizeof(float)))
    return 0;
  output_channels = queue->frame_bytes / sizeof(float);
  source.frames = frames;
  source.channels = channels;
  source.sequential_be = sequential_be;
  source.volume = volume;
  return write_samples(queue, data, frames * output_channels, &source);
}

static size_t queue_mix(xe_audio_queue *queue, float *mix, size_t count, int notify) {
  const void *source;
  const float *samples;
  float wrapped[6];
  size_t total = 0, span, i;
  if (!queue || (!mix && count) || count % (queue->frame_bytes / sizeof(float))) return 0;
  if (queue->group && retro_atomic_load_acquire_int(&queue->group->stopped)) return 0;
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
      if (notify) retro_eventcount_notify(space_event(queue));
      continue;
    }
    span -= span % (queue->frame_bytes / sizeof(float));
    if (span > count - total) span = count - total;
    samples = (const float *)source;
    for (i = 0; i < span; ++i) mix[total + i] += samples[i];
    retro_spsc_read_end(&queue->samples, span * sizeof(float));
    total += span;
    if (notify) retro_eventcount_notify(space_event(queue));
  }
  return total;
}

size_t xe_audio_queue_mix(xe_audio_queue *queue, float *mix, size_t count) {
  return queue_mix(queue, mix, count, 1);
}
size_t xe_audio_registry_mix(xe_audio_registry *registry, float *mix, size_t count) {
  struct xe_audio_slot *slot;
  xe_audio_queue *queue;
  size_t longest = 0, got;
  unsigned i;
  if ((!mix && count) || count > (size_t)-1 / sizeof(float)) return 0;
  if (count) memset(mix, 0, count * sizeof(float));
  if (!registry || count % registry->channels) return 0;
  for (i = 0; i < AUDIO_SLOTS; ++i) {
    slot = &registry->slots[i];
    if (retro_atomic_load_relaxed_int(&slot->state) != SLOT_ACTIVE) continue;
    if (!retro_atomic_cas_int(&slot->state, SLOT_ACTIVE, SLOT_ACTIVE | SLOT_READING)) continue;
    queue = slot->queue;
#ifdef XE_AUDIO_REGISTRY_TEST
    xe_audio_registry_test_lease(queue);
#endif
    got = queue_mix(queue, mix, count, 0);
    if (got > longest) longest = got;
    /* Cleanup belongs to a later cold creation/destruction call, never to
     * this frontend path. Retired slots remain bounded and unavailable until
     * collected. No queue dereference is permitted after releasing the lease. */
    retro_atomic_fetch_and_int(&slot->state, ~SLOT_READING);
  }
  if (longest) retro_eventcount_notify(&registry->space);
  return longest;
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
