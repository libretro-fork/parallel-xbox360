#include "core_input_mailbox.h"
#include "core_audio_cadence.h"
#include <retro_atomic.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef XE_INPUT_MAILBOX_TEST
void xe_input_mailbox_test_pin(void);
#endif
#define PORTS 4
#define SNAPSHOTS 16
#define KEYS 18
#define EVENTS 64
struct snapshot { retro_atomic_int_t readers; xe_input_packet packet; };
struct key_slot { retro_atomic_64_t sequence; xe_input_key key; };
struct input_port {
   struct snapshot snapshots[SNAPSHOTS];
   retro_atomic_int_t current, motors, dirty;
   struct key_slot events[EVENTS];
   uint64_t head;
   retro_atomic_64_t tail;
   uint16_t held[KEYS];
   uint64_t repeat[KEYS];
   size_t held_count;
};
struct xe_input_mailbox {
   struct input_port ports[PORTS];
   xe_audio_cadence cadence;
   uint64_t ticks;
};
xe_input_mailbox *xe_input_mailbox_new(void) {
   xe_input_mailbox *input = (xe_input_mailbox *)calloc(1, sizeof(*input));
   unsigned port, i;
   if (!input) return NULL;
   for (port = 0; port < PORTS; ++port) {
      for (i = 0; i < EVENTS; ++i)
         retro_atomic_64_init(&input->ports[port].events[i].sequence, (int64_t)i);
   }
   return input;
}
void xe_input_mailbox_free(xe_input_mailbox *input) { free(input); }
int xe_input_packet_publish(xe_input_mailbox *input, unsigned port, const xe_input_packet *packet) {
   struct input_port *p;
   unsigned current, index, i;
   struct snapshot *slot;
   if (!input || port >= PORTS || !packet) return 0;
   p = &input->ports[port];
   current = (unsigned)retro_atomic_load_relaxed_int(&p->current);
   for (i = 1; i < SNAPSHOTS; ++i) {
      index = (current + i) & (SNAPSHOTS - 1);
      slot = &p->snapshots[index];
      if (!retro_atomic_cas_int(&slot->readers, 0, -1)) continue;
      slot->packet = *packet;
      retro_atomic_store_release_int(&slot->readers, 0);
      retro_atomic_store_release_int(&p->current, (int)index);
      return 1;
   }
   return 0;
}
const xe_input_packet *xe_input_packet_acquire(xe_input_mailbox *input, unsigned port, xe_input_lease *lease) {
   struct input_port *p;
   struct snapshot *slot;
   int readers;
   unsigned index;
   if (!lease) return NULL;
   lease->slot = NULL;
   if (!input || port >= PORTS) return NULL;
   p = &input->ports[port];
   /* Successful slot CAS acquires the published payload. Index/count probes
    * are optimistic validation only; an extra acquire RMW on MSVC is needless.
    * Recheck current after pinning so an old index cannot expose a slot that
    * the publisher has filled but has not yet published. */
   for (;;) {
      index = (unsigned)retro_atomic_load_relaxed_int(&p->current);
      slot = &p->snapshots[index];
      readers = retro_atomic_load_relaxed_int(&slot->readers);
      if (readers < 0 || readers == INT_MAX ||
            !retro_atomic_cas_int(&slot->readers, readers, readers + 1)) continue;
      if ((unsigned)retro_atomic_load_relaxed_int(&p->current) != index) {
         retro_atomic_fetch_sub_int(&slot->readers, 1);
         continue;
      }
#ifdef XE_INPUT_MAILBOX_TEST
      xe_input_mailbox_test_pin();
#endif
      lease->slot = slot;
      return &slot->packet;
   }
}
void xe_input_packet_release(xe_input_lease *lease) {
   struct snapshot *slot;
   if (!lease || !lease->slot) return;
   slot = (struct snapshot *)lease->slot;
   lease->slot = NULL;
   retro_atomic_fetch_sub_int(&slot->readers, 1);
}
int xe_input_packet_read(xe_input_mailbox *input, unsigned port, xe_input_packet *packet) {
   xe_input_lease lease;
   const xe_input_packet *view;
   if (!packet) return 0;
   view = xe_input_packet_acquire(input, port, &lease);
   if (!view) return 0;
   *packet = *view;
   xe_input_packet_release(&lease);
   return 1;
}
int xe_input_connected(xe_input_mailbox *input, unsigned port) {
   xe_input_lease lease;
   const xe_input_packet *view = xe_input_packet_acquire(input, port, &lease);
   int connected;
   if (!view) return 0;
   connected = view->connected;
   xe_input_packet_release(&lease);
   return connected;
}
void xe_input_begin_run(xe_input_mailbox *input, double fps) {
   if (input) input->ticks += xe_audio_cadence_next(&input->cadence, 48000, fps);
}
int xe_input_key_take(xe_input_mailbox *input, unsigned port, xe_input_key *key) {
   struct input_port *p;
   struct key_slot *slot;
   uint64_t position, sequence;
   if (!input || port >= PORTS || !key) return 0;
   p = &input->ports[port];
   for (;;) {
      position = (uint64_t)retro_atomic_load_acquire_64(&p->tail);
      slot = &p->events[(size_t)position & (EVENTS - 1)];
      sequence = (uint64_t)retro_atomic_load_acquire_64(&slot->sequence);
      if (sequence != position + 1) {
         if ((int64_t)(sequence - position - 1) < 0) return 0;
         continue;
      }
      if (retro_atomic_cas_64(&p->tail, (int64_t)position, (int64_t)(position + 1))) break;
   }
   *key = slot->key;
   retro_atomic_store_release_64(&slot->sequence, (int64_t)(position + EVENTS));
   return 1;
}
static void push_key(xe_input_mailbox *input, unsigned port, uint16_t key, uint16_t flags) {
   struct input_port *p = &input->ports[port];
   struct key_slot *slot = &p->events[(size_t)p->head & (EVENTS - 1)];
   xe_input_key discarded;
   if ((uint64_t)retro_atomic_load_acquire_64(&slot->sequence) != p->head) {
      /* The producer can retire the oldest unclaimed event. If a consumer is
       * paused while holding this slot, drop the new event rather than wait. */
      xe_input_key_take(input, port, &discarded);
      if ((uint64_t)retro_atomic_load_acquire_64(&slot->sequence) != p->head) return;
   }
   slot->key.key = key; slot->key.flags = flags;
   retro_atomic_store_release_64(&slot->sequence, (int64_t)(p->head + 1));
   ++p->head;
}
static size_t find_key(const uint16_t *keys, size_t count, uint16_t key) {
   size_t i;
   for (i = 0; i < count; ++i) if (keys[i] == key) return i;
   return count;
}
void xe_input_keys_update(xe_input_mailbox *input, unsigned port,
      const uint16_t *keys, size_t count, int connected) {
   struct input_port *p;
   uint64_t next[KEYS];
   xe_input_key discarded;
   size_t i, previous;
   if (!input || port >= PORTS || count > KEYS || (!keys && count)) return;
   p = &input->ports[port];
   if (!connected) {
      p->held_count = 0;
      for (i = 0; i < EVENTS && xe_input_key_take(input, port, &discarded); ++i) {}
      return;
   }
   for (i = 0; i < p->held_count; ++i)
      if (find_key(keys, count, p->held[i]) == count) push_key(input, port, p->held[i], 2);
   for (i = 0; i < count; ++i) {
      previous = find_key(p->held, p->held_count, keys[i]);
      if (previous == p->held_count) {
         push_key(input, port, keys[i], 1);
         next[i] = input->ticks + 19200;
      } else {
         next[i] = p->repeat[previous];
         if (input->ticks >= next[i]) {
            push_key(input, port, keys[i], 5);
            next[i] = input->ticks + 4800;
         }
      }
   }
   for (i = 0; i < count; ++i) { p->held[i] = keys[i]; p->repeat[i] = next[i]; }
   p->held_count = count;
}
void xe_input_rumble_post(xe_input_mailbox *input, unsigned port, uint16_t strong, uint16_t weak) {
   if (!input || port >= PORTS) return;
   retro_atomic_store_release_int(&input->ports[port].motors, (int)((uint32_t)strong | ((uint32_t)weak << 16)));
   retro_atomic_store_release_int(&input->ports[port].dirty, 1);
}
int xe_input_rumble_take(xe_input_mailbox *input, unsigned port, uint16_t *strong, uint16_t *weak) {
   uint32_t motors;
   if (!input || port >= PORTS || !strong || !weak ||
         !retro_atomic_exchange_int(&input->ports[port].dirty, 0)) return 0;
   motors = (uint32_t)retro_atomic_load_acquire_int(&input->ports[port].motors);
   *strong = (uint16_t)motors; *weak = (uint16_t)(motors >> 16);
   return 1;
}
