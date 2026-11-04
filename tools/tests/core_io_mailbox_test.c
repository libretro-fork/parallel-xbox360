#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include "core_io_mailbox.h"
#define PRODUCERS 6
#define WINDOW 8
#define COUNT 32768
struct request {
   xe_io_message message;
   retro_atomic_int_t acknowledged;
   unsigned producer, sequence;
};
static struct request requests[PRODUCERS][WINDOW];
static unsigned ids[PRODUCERS], received[PRODUCERS];
static xe_io_mailbox *box;
static void produce(void *opaque) {
   unsigned producer = *(unsigned *)opaque, i;
   for (i = 0; i < COUNT; ++i) {
      struct request *request = &requests[producer][i % WINDOW];
      int previous = i < WINDOW ? 0 : (int)(i - WINDOW + 1);
      while (retro_atomic_load_acquire_int(&request->acknowledged) != previous) {}
      request->producer = producer;
      request->sequence = i;
      request->message.context = request;
      xe_io_mailbox_push(box, &request->message);
      /* The consumer may already have returned/recycled this message. */
   }
}
static void produce_allocated(void *opaque) {
   unsigned producer = *(unsigned *)opaque, i;
   for (i = 0; i < 4096; ++i) {
      struct request *request = (struct request *)malloc(sizeof(*request));
      if (!request) abort();
      request->producer = producer;
      request->sequence = i;
      request->message.context = request;
      xe_io_mailbox_push(box, &request->message);
   }
}
int main(void) {
   sthread_t *threads[PRODUCERS];
   xe_io_message first[3], *message;
   struct request *request;
   unsigned i, j, total = 0;
   box = xe_io_mailbox_new();
   if (!box || xe_io_mailbox_take(box)) return 1;
   for (i = 0; i < 3; ++i) first[i].context = &first[i];
   xe_io_mailbox_push(box, &first[0]);
   xe_io_mailbox_push(box, &first[1]);
   if (xe_io_mailbox_take(box) != &first[0]) return 2;
   xe_io_mailbox_push(box, &first[2]);
   if (xe_io_mailbox_take(box) != &first[1] ||
       xe_io_mailbox_take(box) != &first[2] || xe_io_mailbox_take(box)) return 3;
   for (i = 0; i < PRODUCERS; ++i) {
      ids[i] = i;
      for (j = 0; j < WINDOW; ++j)
         retro_atomic_int_init(&requests[i][j].acknowledged, 0);
      threads[i] = sthread_create(produce, &ids[i]);
      if (!threads[i]) return 4;
   }
   while (total < PRODUCERS * COUNT) {
      message = xe_io_mailbox_take(box);
      if (!message) continue;
      request = (struct request *)message->context;
      if (request->producer >= PRODUCERS ||
          request->sequence != received[request->producer]++) return 5;
      ++total;
      /* Last access: producer may reuse immediately after publication. */
      retro_atomic_store_release_int(&request->acknowledged, (int)request->sequence + 1);
   }
   for (i = 0; i < PRODUCERS; ++i) {
      sthread_join(threads[i]);
      if (received[i] != COUNT) return 6;
   }
   if (xe_io_mailbox_take(box)) return 7;
   total = 0;
   for (i = 0; i < PRODUCERS; ++i) {
      received[i] = 0;
      threads[i] = sthread_create(produce_allocated, &ids[i]);
      if (!threads[i]) return 8;
   }
   while (total < PRODUCERS * 4096) {
      message = xe_io_mailbox_take(box);
      if (!message) continue;
      request = (struct request *)message->context;
      if (request->producer >= PRODUCERS ||
          request->sequence != received[request->producer]++) return 9;
      ++total;
      /* Reclamation may race producers that sampled an old stack head. */
      free(request);
   }
   for (i = 0; i < PRODUCERS; ++i) sthread_join(threads[i]);
   if (xe_io_mailbox_take(box)) return 10;
   xe_io_mailbox_free(box);
   puts("PASS: MPSC FIFO batches, six producers, 196608 reusable messages plus 24576 immediately freed nodes");
   return 0;
}
