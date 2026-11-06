/* Three image slots: one producer, nonblocking serialized consumer leases.
 * Payloads stay in their slots; publication transfers indices only. */
#ifndef XE_CORE_VIDEO_MAILBOX_H
#define XE_CORE_VIDEO_MAILBOX_H
#include <stdint.h>
#include <retro_atomic.h>
#include <retro_inline.h>

typedef struct xe_video_mailbox {
  retro_atomic_int_t acquired_ready;
  retro_atomic_int_t consumer;
  uint32_t writable;
} xe_video_mailbox;

/* Cold initialization, before either side starts. */
static INLINE void xe_video_mailbox_init(xe_video_mailbox *mailbox) {
  retro_atomic_store_relaxed_int(&mailbox->acquired_ready, 0);
  retro_atomic_store_relaxed_int(&mailbox->consumer, 0);
  mailbox->writable = 1;
}

/* Publish the completed writable slot and acquire a free slot for reuse.
 * Only the producer touches writable. Never overwrite the acquired slot. */
static INLINE void xe_video_mailbox_publish(xe_video_mailbox *mailbox) {
  int previous;
  uint32_t acquired;
  do {
    previous = retro_atomic_load_relaxed_int(&mailbox->acquired_ready);
  } while (!retro_atomic_cas_int(&mailbox->acquired_ready, previous,
      (previous & 3) | (int)(mailbox->writable << 2)));
  acquired = (uint32_t)previous & 3;
  if (acquired == mailbox->writable)
    mailbox->writable = (mailbox->writable + 1) % 3;
  else
    mailbox->writable = (3 - acquired - mailbox->writable) % 3;
}

/* UINT32_MAX means another consumer owns a lease. Do not wait or release it.
 * Otherwise keep this lease through ALL reads of the slot and its properties.
 * Multiple frontend/capture callers may try, but only one can acquire at once. */
static INLINE uint32_t xe_video_mailbox_read_begin(xe_video_mailbox *mailbox) {
  int previous, desired;
  if (!retro_atomic_cas_int(&mailbox->consumer, 0, 1)) return UINT32_MAX;
  do {
    previous = retro_atomic_load_relaxed_int(&mailbox->acquired_ready);
    desired = (previous & ~3) | (previous >> 2);
    /* The consumer lease acquire orders a repeated read of the same slot. */
    if (previous == desired) break;
  } while (!retro_atomic_cas_int(&mailbox->acquired_ready, previous, desired));
  return (uint32_t)desired & 3;
}

static INLINE void xe_video_mailbox_read_end(xe_video_mailbox *mailbox) {
  retro_atomic_store_release_int(&mailbox->consumer, 0);
}
#endif
