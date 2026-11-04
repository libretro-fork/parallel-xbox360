#ifndef XE_CORE_INPUT_MAILBOX_H
#define XE_CORE_INPUT_MAILBOX_H
#include "core_input_analog.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_input_packet {
   uint32_t packet;
   uint16_t buttons;
   xe_input_analog analog;
   int connected;
} xe_input_packet;
typedef struct xe_input_key { uint16_t key, flags; } xe_input_key;
typedef struct xe_input_mailbox xe_input_mailbox;
/* One frontend publisher, multiple guest readers. Cold allocation only.
 * Readers pin immutable slots. A publisher never waits for a pinned slot;
 * saturation retains the previous published packet and returns zero. */
xe_input_mailbox *xe_input_mailbox_new(void);
/* Quiesce all publishers/readers and release every lease before free. */
void xe_input_mailbox_free(xe_input_mailbox *input);
/* Do not copy a live lease. Release before owner destruction; publication
 * may reuse the slot immediately after release. */
typedef struct xe_input_lease { void *slot; } xe_input_lease;
const xe_input_packet *xe_input_packet_acquire(xe_input_mailbox *input, unsigned port, xe_input_lease *lease);
void xe_input_packet_release(xe_input_lease *lease);
int xe_input_connected(xe_input_mailbox *input, unsigned port);
int xe_input_packet_publish(xe_input_mailbox *input, unsigned port, const xe_input_packet *packet);
int xe_input_packet_read(xe_input_mailbox *input, unsigned port, xe_input_packet *packet);
void xe_input_begin_run(xe_input_mailbox *input, double fps);
/* At most 18 held keys per port; bounded 64-event FIFO, oldest overflow drops.
 * Only the frontend updates keys. Any guest thread may consume a key. A slot
 * pinned by a preempted consumer may force a new event to be dropped. */
void xe_input_keys_update(xe_input_mailbox *input, unsigned port,
      const uint16_t *keys, size_t count, int connected);
int xe_input_key_take(xe_input_mailbox *input, unsigned port, xe_input_key *key);
/* Multiple guest writers; latest complete motor pair is consumed by frontend. */
void xe_input_rumble_post(xe_input_mailbox *input, unsigned port, uint16_t strong, uint16_t weak);
int xe_input_rumble_take(xe_input_mailbox *input, unsigned port, uint16_t *strong, uint16_t *weak);
#ifdef __cplusplus
}
#endif
#endif
