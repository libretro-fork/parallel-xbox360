#include "core_audio_queue.h"
#include "core_audio_convert.h"
#include "core_audio_packet.h"
#include "core_audio_borrow.h"
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
  retro_eventcount_t data;
  retro_atomic_int_t stopped, references;
  unsigned channels;
  int quantum_mode;
  size_t carry_offset,carry_count;
  float carry[256*6];
};
struct xe_audio_group {
  xe_audio_registry *registry;
  retro_atomic_int_t stopped, references;
  int frontend_driven;
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
  size_t produced_frames;
  xe_audio_packet packet;
  int borrowed;
  xe_audio_borrow loan;
};

static xe_audio_queue *queue_allocate(unsigned channels, int local_space, size_t max_frames, int borrowed) {
  xe_audio_queue *queue;
  if ((channels != 2 && channels != 6) || !max_frames || max_frames > SIZE_MAX / (6 * sizeof(float))) return NULL;
  queue = (xe_audio_queue *)calloc(1, sizeof(*queue));
  if (!queue) return NULL;
  /* Storage capacity only, never a production/latency target. The frontend
   * request bounds live output; a final source quantum may cross its boundary. */
  queue->limit = max_frames * channels * sizeof(float);
  queue->frame_bytes = channels * sizeof(float);
  queue->borrowed=borrowed;
  if (!borrowed) {
#ifdef XE_AUDIO_QUEUE_PLAIN
  if (!retro_spsc_init(&queue->samples, queue->limit)) {
#else
  if (!retro_spsc_init_mirrored(&queue->samples, queue->limit)) {
#endif
    free(queue); return NULL;
  }
  }
  if (local_space && !retro_eventcount_init(&queue->space)) {
    retro_spsc_free(&queue->samples); free(queue); return NULL;
  }
  queue->local_space_initialized = local_space;
  xe_audio_queue_volume(queue, 1.0f);
  return queue;
}

xe_audio_queue *xe_audio_queue_new(unsigned channels) { return queue_allocate(channels, 1, 4096, 0); }

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
  if (queue->borrowed) xe_audio_borrow_cancel(&queue->loan,space_event(queue));
  xe_audio_packet_cancel(&queue->packet);
  retro_eventcount_notify(space_event(queue));
  if (queue->registry) retro_eventcount_notify(&queue->registry->data);
}

static void queue_destroy(xe_audio_queue *queue) {
  xe_audio_group *group;
  if (!queue) return;
  group = queue->group;
  xe_audio_packet_free(&queue->packet);
  if (queue->local_space_initialized) retro_eventcount_free(&queue->space);
  retro_spsc_free(&queue->samples);
  free(queue);
  if (group) group_release(group);
}

static void registry_release(xe_audio_registry *registry) {
  if (retro_atomic_fetch_sub_int(&registry->references, 1) != 1) return;
  retro_eventcount_free(&registry->space);
  retro_eventcount_free(&registry->data);
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
  xe_audio_queue_stop(queue);
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
  if (!retro_eventcount_init(&registry->data)) {
    retro_eventcount_free(&registry->space); free(registry); return NULL;
  }
  registry->channels = channels;
  retro_atomic_store_release_int(&registry->references, 1);
  return registry;
}
void xe_audio_registry_stop(xe_audio_registry *registry) {
  if (!registry) return;
  retro_atomic_store_release_int(&registry->stopped, 1);
  /* No stream pointer traversal, including when a stream is being retired. */
  retro_eventcount_notify(&registry->space);
  retro_eventcount_notify(&registry->data);
}
void xe_audio_registry_free(xe_audio_registry *registry) {
  if (!registry) return;
  xe_audio_registry_stop(registry);
  collect_retired(registry);
  registry_release(registry);
}
static xe_audio_queue *registered_queue_new(xe_audio_registry *registry, xe_audio_group *group, size_t max_frames,
    void (*complete)(void *, int), void *context, int borrowed) {
  xe_audio_queue *queue;
  unsigned i;
  if (!registry || retro_atomic_load_acquire_int(&registry->stopped) ||
      (group && retro_atomic_load_acquire_int(&group->stopped))) return NULL;
  collect_retired(registry);
  queue = queue_allocate(registry->channels, 0, max_frames, borrowed);
  if (!queue) return NULL;
  if (complete) {
    if (!xe_audio_packet_init(&queue->packet,complete,context)) {
      queue_destroy(queue); return NULL;
    }
    queue->packet.changed=&registry->data;
  }
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
  return registered_queue_new(registry, NULL, 4096, NULL, NULL, 0);
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
xe_audio_group *xe_audio_group_new_frontend(xe_audio_registry *registry) {
  xe_audio_group *group = xe_audio_group_new(registry);
  if (group) group->frontend_driven = 1;
  return group;
}
void xe_audio_group_stop(xe_audio_group *group) {
  if (!group) return;
  retro_atomic_store_release_int(&group->stopped, 1);
  retro_eventcount_notify(&group->registry->space);
  retro_eventcount_notify(&group->registry->data);
}
void xe_audio_group_free(xe_audio_group *group) {
  if (!group) return;
  xe_audio_group_stop(group);
  collect_retired(group->registry);
  group_release(group);
}
xe_audio_queue *xe_audio_group_queue_new(xe_audio_group *group) {
  return group ? registered_queue_new(group->registry, group, 4096, NULL, NULL, 0) : NULL;
}

static size_t room(const xe_audio_queue *queue) {
  size_t used = queue->samples.capacity - retro_spsc_write_avail(&queue->samples);
  return used < queue->limit ? queue->limit - used : 0;
}

xe_audio_queue *xe_audio_group_queue_new_packet(xe_audio_group *group, size_t max_frames,
      void (*complete)(void *, int), void *context) {
  if (!group || !complete) return NULL;
  return registered_queue_new(group->registry,group,max_frames,complete,context,0);
}
xe_audio_queue *xe_audio_group_queue_new_borrowed(xe_audio_group *group,size_t frames,
      void (*complete)(void *,int),void *context) {
  if (!group || !complete) return NULL;
  return registered_queue_new(group->registry,group,frames,complete,context,1);
}
int xe_audio_queue_finish_packet(xe_audio_queue *queue) {
  int result;
  if (!queue || stopped(queue)) return 0;
  result=xe_audio_packet_publish(&queue->packet,queue->produced_frames);
  if (queue->registry) retro_eventcount_notify(&queue->registry->data);
  return result;
}
/* Media producer waits on the same frontend demand predicate as native ports. */
int xe_audio_queue_wait_request(xe_audio_queue *queue) {
  int key;
  if (!queue || !queue->registry) return 0;
  for (;;) {
    key=retro_eventcount_prepare_wait(&queue->registry->data);
    if (stopped(queue) || xe_audio_queue_paused(queue)) {
      retro_eventcount_cancel_wait(&queue->registry->data); return 0;
    }
    if (retro_atomic_load_acquire_int(&queue->packet.state)&XE_PACKET_REQUESTED) {
      retro_eventcount_cancel_wait(&queue->registry->data); return 1;
    }
    retro_eventcount_commit_wait(&queue->registry->data,key);
  }
}
int xe_audio_queue_can_write_frames(const xe_audio_queue *queue, size_t frames) {
  if (!queue || stopped(queue) || xe_audio_queue_paused(queue) ||
      frames > SIZE_MAX / queue->frame_bytes) return 0;
  if (queue->borrowed) return frames*queue->frame_bytes<=queue->limit &&
      !(retro_atomic_load_acquire_int(&queue->loan.state)&(XE_AUDIO_LOAN_ACTIVE|XE_AUDIO_LOAN_CANCELLED));
  return frames * queue->frame_bytes <= room(queue);
}

size_t xe_audio_queue_submit_borrowed(xe_audio_queue *queue,const float *samples,
      size_t frames,unsigned channels,int sequential_be,float volume) {
  int key;
  if (!queue || !queue->borrowed || !samples || !frames ||
      (channels!=2 && channels!=6) || (sequential_be && channels!=6) ||
      frames>queue->limit/queue->frame_bytes || !xe_audio_queue_wait_request(queue)) return 0;
  queue->produced_frames+=frames;
  if (!xe_audio_packet_publish(&queue->packet,queue->produced_frames) ||
      !xe_audio_borrow_publish(&queue->loan,samples,frames,channels,sequential_be,volume)) return 0;
  retro_eventcount_notify(&queue->registry->data);
  for (;;) {
    key=retro_eventcount_prepare_wait(space_event(queue));
    if (stopped(queue)) {
      retro_eventcount_cancel_wait(space_event(queue));
      xe_audio_borrow_cancel(&queue->loan,space_event(queue)); return 0;
    }
    if (!(retro_atomic_load_acquire_int(&queue->loan.state)&XE_AUDIO_LOAN_ACTIVE)) {
      retro_eventcount_cancel_wait(space_event(queue)); return frames;
    }
    retro_eventcount_commit_wait(space_event(queue),key);
  }
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
  while (written < count && !stopped(queue) && !xe_audio_queue_paused(queue)) {
    available = room(queue);
    if (!available) {
      key = retro_eventcount_prepare_wait(space_event(queue));
      if (room(queue) || stopped(queue) || xe_audio_queue_paused(queue))
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
      if (queue->registry) retro_eventcount_notify(&queue->registry->data);
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
    if (queue->registry) retro_eventcount_notify(&queue->registry->data);
  }
  queue->produced_frames += written / output_channels;
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

static size_t borrowed_mix(xe_audio_queue *queue,float *mix,size_t count) {
  size_t total=0,frames,channels=queue->frame_bytes/sizeof(float);
  int key;
  while (total<count && !stopped(queue)) {
    if (xe_audio_borrow_read_begin(&queue->loan)) {
      frames=queue->loan.frames-queue->loan.offset;
      if (frames>(count-total)/channels) frames=(count-total)/channels;
      xe_audio_mix_borrowed(mix+total,queue->loan.samples,queue->loan.frames,
          queue->loan.offset,frames,queue->loan.channels,(unsigned)channels,
          queue->loan.sequential_be,queue->loan.volume);
      xe_audio_borrow_read_end(&queue->loan,frames);
      xe_audio_packet_consume(&queue->packet,frames);
      retro_eventcount_notify(space_event(queue));
      total+=frames*channels;
      continue;
    }
    if (xe_audio_queue_paused(queue)) break;
    key=retro_eventcount_prepare_wait(&queue->registry->data);
    xe_audio_packet_request(&queue->packet);
    if ((retro_atomic_load_acquire_int(&queue->loan.state)&XE_AUDIO_LOAN_ACTIVE) ||
        (retro_atomic_load_acquire_int(&queue->packet.state)&XE_PACKET_READY) ||
        stopped(queue) || xe_audio_queue_paused(queue))
      retro_eventcount_cancel_wait(&queue->registry->data);
    else retro_eventcount_commit_wait(&queue->registry->data,key);
  }
  return total;
}
static size_t queue_mix(xe_audio_queue *queue, float *mix, size_t count, int notify) {
  const void *source;
  const float *samples;
  float wrapped[6];
  size_t total = 0, span, i;
  int key;
  if (!queue || (!mix && count) || count % (queue->frame_bytes / sizeof(float))) return 0;
  if (queue->group && retro_atomic_load_acquire_int(&queue->group->stopped)) return 0;
  if (queue->borrowed) return borrowed_mix(queue,mix,count);
  while (total < count) {
    span = retro_spsc_read_begin(&queue->samples, &source) / sizeof(float);
    if (!span) {
      if (!queue->group || !queue->group->frontend_driven ||
          !retro_atomic_load_acquire_int(&queue->packet.initialized) ||
          stopped(queue) || xe_audio_queue_paused(queue)) break;
      key = retro_eventcount_prepare_wait(&queue->registry->data);
      xe_audio_packet_request(&queue->packet);
      if ((retro_atomic_load_acquire_int(&queue->packet.state)&XE_PACKET_READY) ||
          retro_spsc_read_avail(&queue->samples) || stopped(queue) ||
          xe_audio_queue_paused(queue))
        retro_eventcount_cancel_wait(&queue->registry->data);
      else retro_eventcount_commit_wait(&queue->registry->data, key);
      continue;
    }
    if (span < queue->frame_bytes / sizeof(float)) {
      /* Required only for a physical wrap on non-mirrored backends. */
      retro_spsc_read_end(&queue->samples, 0);
      retro_spsc_peek(&queue->samples, wrapped, queue->frame_bytes);
      span = queue->frame_bytes / sizeof(float);
      for (i = 0; i < span; ++i) mix[total + i] += wrapped[i];
      retro_spsc_skip(&queue->samples, queue->frame_bytes);
      xe_audio_packet_consume(&queue->packet, 1);
      total += span;
      if (notify || (queue->group && queue->group->frontend_driven))
      retro_eventcount_notify(space_event(queue));
      continue;
    }
    span -= span % (queue->frame_bytes / sizeof(float));
    if (span > count - total) span = count - total;
    samples = (const float *)source;
    for (i = 0; i < span; ++i) mix[total + i] += samples[i];
    retro_spsc_read_end(&queue->samples, span * sizeof(float));
    xe_audio_packet_consume(&queue->packet, span * sizeof(float) / queue->frame_bytes);
    total += span;
    if (notify || (queue->group && queue->group->frontend_driven))
      retro_eventcount_notify(space_event(queue));
  }
  return total;
}

size_t xe_audio_queue_mix(xe_audio_queue *queue, float *mix, size_t count) {
  return queue_mix(queue, mix, count, 1);
}
size_t xe_audio_registry_mix(xe_audio_registry *registry,float *mix,size_t count) {
  xe_audio_queue *queues[AUDIO_SLOTS];
  unsigned slots[AUDIO_SLOTS],active=0,i;
  int exhausted[AUDIO_SLOTS];
  size_t offset=0,chunk,got,longest=0,take,remaining;
  float *destination;
  struct xe_audio_slot *slot;
  if ((!mix && count) || count>SIZE_MAX/sizeof(float)) return 0;
  if (count) memset(mix,0,count*sizeof(float));
  if (!registry || count%registry->channels || retro_atomic_load_acquire_int(&registry->stopped)) return 0;
  if (registry->carry_count) {
    take=registry->carry_count<count?registry->carry_count:count;
    memcpy(mix,registry->carry+registry->carry_offset,take*sizeof(float));
    registry->carry_offset+=take;registry->carry_count-=take;
    offset=take;longest=take;
  }
  if (offset==count) return longest;
  /* Pin one bounded snapshot; source creation takes effect next run. */
  for (i=0;i<AUDIO_SLOTS;++i) {
    slot=&registry->slots[i];
    if (!retro_atomic_cas_int(&slot->state,SLOT_ACTIVE,SLOT_ACTIVE|SLOT_READING)) continue;
    queues[active]=slot->queue;slots[active]=i;exhausted[active++]=0;
    if (slot->queue->borrowed) registry->quantum_mode=1;
#ifdef XE_AUDIO_REGISTRY_TEST
    xe_audio_registry_test_lease(slot->queue);
#endif
  }
  while (offset<count) {
    remaining=count-offset;
    chunk=256*registry->channels;
    if (!registry->quantum_mode && chunk>remaining) chunk=remaining;
    destination=chunk<=remaining?mix+offset:registry->carry;
    if (destination==registry->carry) memset(destination,0,chunk*sizeof(float));
    for (i=0;i<active;++i) {
      if (exhausted[i] || stopped(queues[i]) || xe_audio_queue_paused(queues[i])) continue;
      if (queues[i]->borrowed || !retro_spsc_read_avail(&queues[i]->samples))
        xe_audio_packet_request(&queues[i]->packet);
    }
    for (i=0;i<active;++i) {
      if (exhausted[i]) continue;
      got=queue_mix(queues[i],destination,chunk,0);
      if (got<chunk) exhausted[i]=1;
      take=got<remaining?got:remaining;
      if (offset+take>longest && take) longest=offset+take;
    }
    if (chunk>remaining) {
      memcpy(mix+offset,registry->carry,remaining*sizeof(float));
      registry->carry_offset=remaining;registry->carry_count=chunk-remaining;
      offset=count;
    } else offset+=chunk;
  }
  for (i=0;i<active;++i)
    retro_atomic_fetch_and_int(&registry->slots[slots[i]].state,~SLOT_READING);
  if (longest) retro_eventcount_notify(&registry->space);
  return registry->quantum_mode?count:longest;
}

void xe_audio_queue_pause(xe_audio_queue *queue, int paused) {
  if (queue) {
    retro_atomic_store_release_int(&queue->paused, paused != 0);
    retro_eventcount_notify(space_event(queue));
    if (queue->registry) retro_eventcount_notify(&queue->registry->data);
  }
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
