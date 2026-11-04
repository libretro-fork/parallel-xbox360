/* Concurrent fixed-slot lifetime and root-owner release; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include "core_audio_queue.h"
static xe_audio_registry *registry;
static xe_audio_queue *leased;
static retro_atomic_int_t entered, released, freed, done, failed;
static retro_eventcount_t event;
static float lease_output[6];

static int wait_flag(retro_atomic_int_t *flag) {
  int key;
  int64_t deadline = cpu_features_get_time_usec() + 2000000;
  int64_t remaining;
  for (;;) {
    key = retro_eventcount_prepare_wait(&event);
    if (retro_atomic_load_acquire_int(flag)) {
      retro_eventcount_cancel_wait(&event); return 1;
    }
    remaining = deadline - cpu_features_get_time_usec();
    if (remaining <= 0) { retro_eventcount_cancel_wait(&event); break; }
    retro_eventcount_commit_wait_timeout(&event, key, remaining);
  }
  return retro_atomic_load_acquire_int(flag);
}
void xe_audio_registry_test_lease(xe_audio_queue *queue) {
  if (queue != leased) return;
  retro_atomic_store_release_int(&entered, 1);
  retro_eventcount_notify(&event);
  if (!wait_flag(&released)) retro_atomic_store_release_int(&failed, 1);
}
static void read_lease(void *unused) {
  (void)unused;
  xe_audio_registry_mix(registry, lease_output, 6);
}
static void free_lease(void *unused) {
  (void)unused;
  xe_audio_queue_free(leased);
  retro_atomic_store_release_int(&freed, 1);
  retro_eventcount_notify(&event);
}
static int retirement_test(void) {
  xe_audio_queue *queues[16];
  sthread_t *reader, *retirer;
  float frame[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  unsigned i;
  int success;
  registry = xe_audio_registry_new(6); if (!registry) return 0;
  leased = xe_audio_registry_queue_new(registry); if (!leased) return 0;
  xe_audio_queue_write(leased, frame, 6);
  reader = sthread_create(read_lease, NULL); if (!reader || !wait_flag(&entered)) return 0;
  retirer = sthread_create(free_lease, NULL); if (!retirer) return 0;
  success = wait_flag(&freed);
  for (i = 0; i < 15; ++i) {
    queues[i] = xe_audio_registry_queue_new(registry);
    if (!queues[i]) success = 0;
  }
  /* A live retired lease must still occupy its slot. */
  queues[15] = xe_audio_registry_queue_new(registry);
  if (queues[15]) { success = 0; xe_audio_queue_free(queues[15]); }
  retro_atomic_store_release_int(&released, 1);
  retro_eventcount_notify(&event);
  sthread_join(reader); sthread_join(retirer);
  leased = NULL;
  if (retro_atomic_load_acquire_int(&failed)) success = 0;
  for (i = 0; i < 6; ++i) if (lease_output[i] != 0.5f) success = 0;
  queues[15] = xe_audio_registry_queue_new(registry);
  if (!queues[15]) success = 0;
  for (i = 0; i < 16; ++i) xe_audio_queue_free(queues[i]);
  xe_audio_registry_free(registry);
  return success;
}
static int mixing_test(unsigned channels) {
  xe_audio_queue *queues[8];
  float input[256 * 6], output[384 * 6 + 2];
  unsigned i, j;
  size_t count = 384 * channels;
  registry = xe_audio_registry_new(channels); if (!registry) return 0;
  for (i = 0; i < 8; ++i) {
    queues[i] = xe_audio_registry_queue_new(registry); if (!queues[i]) return 0;
    for (j = 0; j < 256 * channels; ++j) input[j] = (float)i / 16.0f;
    xe_audio_queue_write(queues[i], input, (i & 1 ? 128 : 256) * channels);
  }
  output[0] = output[count + 1] = 99.0f;
  if (xe_audio_registry_mix(registry, output + 1, count) != 256 * channels) return 0;
  for (j = 0; j < count; ++j) {
    float expected = j < 128 * channels ? 1.75f : j < 256 * channels ? 0.75f : 0.0f;
    if (output[j + 1] != expected) return 0;
  }
  if (output[0] != 99.0f || output[count + 1] != 99.0f) return 0;
  xe_audio_registry_stop(registry);
  if (xe_audio_registry_queue_new(registry)) return 0;
  xe_audio_registry_free(registry);
  /* Queues retain their registry even after its root owner is gone. */
  for (i = 0; i < 8; ++i) {
    if (xe_audio_queue_write(queues[i], input, channels)) return 0;
    xe_audio_queue_free(queues[i]);
  }
  return 1;
}
static void churn(void *unused) {
  xe_audio_queue *queue;
  xe_audio_group *group;
  float frame[6] = {0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f};
  unsigned i;
  (void)unused;
  for (i = 0; i < 1000; ++i) {
    group = xe_audio_group_new(registry);
    queue = xe_audio_group_queue_new(group);
    if (!queue || xe_audio_queue_write(queue, frame, 6) != 6) {
      retro_atomic_store_release_int(&failed, 1); break;
    }
    xe_audio_group_free(group);
    xe_audio_queue_free(queue);
  }
  retro_atomic_fetch_add_int(&done, 1);
}
static int churn_test(void) {
  sthread_t *threads[4];
  float output[6];
  unsigned i;
  registry = xe_audio_registry_new(6); if (!registry) return 0;
  for (i = 0; i < 4; ++i) { threads[i] = sthread_create(churn, NULL); if (!threads[i]) return 0; }
  while (retro_atomic_load_acquire_int(&done) != 4) {
    xe_audio_registry_mix(registry, output, 6);
    for (i = 0; i < 6; ++i) if (!(output[i] >= 0 && output[i] <= 2)) return 0;
  }
  for (i = 0; i < 4; ++i) sthread_join(threads[i]);
  xe_audio_registry_free(registry);
  return !retro_atomic_load_acquire_int(&failed);
}
struct blocked {
  xe_audio_queue *queue;
  retro_atomic_int_t started, finished;
  size_t written;
};
static void blocked_write(void *opaque) {
  struct blocked *job = (struct blocked *)opaque;
  float frame[6] = {1, 1, 1, 1, 1, 1};
  retro_atomic_store_release_int(&job->started, 1);
  retro_eventcount_notify(&event);
  job->written = xe_audio_queue_write(job->queue, frame, 6);
  retro_atomic_store_release_int(&job->finished, 1);
  retro_eventcount_notify(&event);
}
static int root_cancel_test(void) {
  struct blocked jobs[8];
  sthread_t *threads[8];
  float samples[4096 * 6];
  unsigned i;
  int success = 1;
  memset(jobs, 0, sizeof(jobs)); memset(samples, 0, sizeof(samples));
  registry = xe_audio_registry_new(6); if (!registry) return 0;
  for (i = 0; i < 8; ++i) {
    jobs[i].queue = xe_audio_registry_queue_new(registry);
    if (!jobs[i].queue || xe_audio_queue_write(jobs[i].queue, samples, 4096 * 6) != 4096 * 6) return 0;
    threads[i] = sthread_create(blocked_write, &jobs[i]); if (!threads[i]) return 0;
    if (!wait_flag(&jobs[i].started)) return 0;
  }
  xe_audio_registry_free(registry);
  for (i = 0; i < 8; ++i) {
    if (!wait_flag(&jobs[i].finished)) success = 0;
    sthread_join(threads[i]);
    if (jobs[i].written) success = 0;
    xe_audio_queue_free(jobs[i].queue);
  }
  return success;
}
struct closing_group { xe_audio_group *group; xe_audio_queue *queue; };
static void close_group(void *opaque) {
  struct closing_group *closing = (struct closing_group *)opaque;
  xe_audio_group_free(closing->group);
  xe_audio_queue_free(closing->queue);
  retro_atomic_store_release_int(&freed, 1);
  retro_eventcount_notify(&event);
}
static int group_rebuild_test(void) {
  struct closing_group closing;
  xe_audio_group *next;
  xe_audio_queue *queue;
  sthread_t *reader, *closer;
  float frame[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  unsigned i;
  int success;
  retro_atomic_store_release_int(&entered, 0);
  retro_atomic_store_release_int(&released, 0);
  retro_atomic_store_release_int(&freed, 0);
  registry = xe_audio_registry_new(6);
  closing.group = xe_audio_group_new(registry);
  closing.queue = leased = xe_audio_group_queue_new(closing.group);
  if (!closing.queue) return 0;
  xe_audio_queue_write(leased, frame, 6);
  reader = sthread_create(read_lease, NULL); if (!reader || !wait_flag(&entered)) return 0;
  closer = sthread_create(close_group, &closing); if (!closer) return 0;
  success = wait_flag(&freed);
  next = xe_audio_group_new(registry);
  queue = xe_audio_group_queue_new(next); if (!queue) success = 0;
  for (i = 0; i < 6; ++i) frame[i] = 1.25f;
  xe_audio_queue_write(queue, frame, 6);
  retro_atomic_store_release_int(&released, 1);
  retro_eventcount_notify(&event);
  sthread_join(reader); sthread_join(closer);
  leased = NULL;
  if (retro_atomic_load_acquire_int(&failed)) success = 0;
  /* The stopped generation contributes no stale tail; the replacement works
   * on the same frontend-owned registry while its predecessor is retired. */
  for (i = 0; i < 6; ++i) if (lease_output[i] != 1.25f) success = 0;
  xe_audio_queue_write(queue, frame, 6);
  if (xe_audio_registry_mix(registry, lease_output, 6) != 6) success = 0;
  for (i = 0; i < 6; ++i) if (lease_output[i] != 1.25f) success = 0;
  xe_audio_queue_free(queue); xe_audio_group_free(next);
  xe_audio_registry_free(registry);
  return success;
}
static int group_cancel_test(void) {
  xe_audio_group *old, *next;
  xe_audio_queue *next_queue;
  struct blocked job;
  sthread_t *thread;
  float samples[4096 * 6], output[6];
  unsigned i;
  int success;
  memset(&job, 0, sizeof(job));
  registry = xe_audio_registry_new(6);
  old = xe_audio_group_new(registry); next = xe_audio_group_new(registry);
  job.queue = xe_audio_group_queue_new(old);
  next_queue = xe_audio_group_queue_new(next);
  if (!job.queue || !next_queue) return 0;
  for (i = 0; i < 4096 * 6; ++i) samples[i] = 1.0f;
  xe_audio_queue_write(job.queue, samples, 4096 * 6);
  thread = sthread_create(blocked_write, &job); if (!thread || !wait_flag(&job.started)) return 0;
  xe_audio_group_free(old);
  success = wait_flag(&job.finished);
  sthread_join(thread);
  if (job.written) success = 0;
  for (i = 0; i < 6; ++i) samples[i] = 0.25f;
  xe_audio_queue_write(next_queue, samples, 6);
  if (xe_audio_registry_mix(registry, output, 6) != 6) success = 0;
  for (i = 0; i < 6; ++i) if (output[i] != 0.25f) success = 0;
  xe_audio_queue_free(job.queue); xe_audio_queue_free(next_queue);
  xe_audio_group_free(next); xe_audio_registry_free(registry);
  return success;
}
int main(void) {
  if (!retro_eventcount_init(&event)) return 1;
  if (!retirement_test()) return 2;
  if (!mixing_test(2) || !mixing_test(6)) return 3;
  if (!churn_test()) return 4;
  if (!root_cancel_test()) return 5;
  if (!group_rebuild_test() || !group_cancel_test()) return 6;
  retro_eventcount_free(&event);
  puts("PASS: nonblocking retirement, bounded leases, registry ownership, exact mixing and 4000 concurrent lifetimes");
  return 0;
}
