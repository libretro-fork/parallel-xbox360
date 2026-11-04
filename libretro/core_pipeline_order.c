#include "core_pipeline_order.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdlib.h>
#include <limits.h>

typedef struct xe_pipeline_order_slot {
   retro_atomic_int_t state;
   size_t ticket;
} xe_pipeline_order_slot;
struct xe_pipeline_order {
   xe_pipeline_order_slot *slots;
   unsigned char *payloads;
   size_t capacity, stride, next, cursor;
   retro_atomic_int_t publishing;
   retro_eventcount_t space;
   xe_pipeline_order_sink publish;
   void *opaque;
};
static size_t xe_order_advance(size_t ticket)
{
   ++ticket;
   return ticket ? ticket : 1;
}
static xe_pipeline_order_slot *xe_order_slot(xe_pipeline_order *order, size_t ticket)
{
   return &order->slots[(ticket - 1) & (order->capacity - 1)];
}
static void *xe_order_data(xe_pipeline_order *order, size_t ticket)
{
   return order->payloads + ((ticket - 1) & (order->capacity - 1)) * order->stride;
}
xe_pipeline_order *xe_pipeline_order_new(size_t capacity, size_t payload_size,
      xe_pipeline_order_sink publish, void *opaque)
{
   xe_pipeline_order *order;
   size_t stride, i;
   if (!capacity || capacity > INT_MAX / 4 ||
         (capacity & (capacity - 1)) || !payload_size || !publish ||
         capacity > (size_t)-1 / sizeof(xe_pipeline_order_slot) ||
         payload_size > (size_t)-1 - 15) return NULL;
   stride = (payload_size + 15) & ~(size_t)15;
   if (capacity > (size_t)-1 / stride) return NULL;
   order = (xe_pipeline_order *)calloc(1, sizeof(*order));
   if (!order) return NULL;
   order->slots = (xe_pipeline_order_slot *)calloc(capacity, sizeof(*order->slots));
   order->payloads = (unsigned char *)malloc(capacity * stride);
   if (!order->slots || !order->payloads || !retro_eventcount_init(&order->space)) {
      xe_pipeline_order_free(order);
      return NULL;
   }
   for (i = 0; i < capacity; ++i)
      retro_atomic_int_init(&order->slots[i].state, 0);
   retro_atomic_int_init(&order->publishing, 0);
   order->capacity = capacity;
   order->stride = stride;
   order->next = order->cursor = 1;
   order->publish = publish;
   order->opaque = opaque;
   return order;
}
size_t xe_pipeline_order_reserve(xe_pipeline_order *order)
{
   xe_pipeline_order_slot *slot;
   size_t ticket;
   int key;
   if (!order) return 0;
   ticket = order->next;
   slot = xe_order_slot(order, ticket);
   while (retro_atomic_load_acquire_int(&slot->state)) {
      key = retro_eventcount_prepare_wait(&order->space);
      if (!retro_atomic_load_acquire_int(&slot->state))
         retro_eventcount_cancel_wait(&order->space);
      else retro_eventcount_commit_wait(&order->space, key);
   }
   slot->ticket = ticket;
   retro_atomic_store_release_int(&slot->state, 1);
   order->next = xe_order_advance(ticket);
   return ticket;
}
void *xe_pipeline_order_payload(xe_pipeline_order *order, size_t ticket)
{
   xe_pipeline_order_slot *slot;
   if (!order || !ticket) return NULL;
   slot = xe_order_slot(order, ticket);
   if (retro_atomic_load_acquire_int(&slot->state) != 1 || slot->ticket != ticket)
      return NULL;
   return xe_order_data(order, ticket);
}
int xe_pipeline_order_complete(xe_pipeline_order *order, size_t ticket)
{
   xe_pipeline_order_slot *slot;
   size_t drained;
   int pending = 1, previous;
   if (!xe_pipeline_order_payload(order, ticket)) return 0;
   slot = xe_order_slot(order, ticket);
   retro_atomic_store_release_int(&slot->state, 2);
   /* Each completed slot contributes work before returning. The last atomic
    * subtraction either observes that work or hands ownership to its finisher;
    * unlike a release-store followed by a separate slot load, it cannot miss
    * a concurrent finisher on architectures with store/load reordering. */
   if (retro_atomic_fetch_add_int(&order->publishing, 1) != 0) return 1;
   for (;;) {
      for (drained = 0; drained < order->capacity; ++drained) {
         slot = xe_order_slot(order, order->cursor);
         if (retro_atomic_load_acquire_int(&slot->state) != 2 ||
               slot->ticket != order->cursor) break;
         order->publish(order->opaque, xe_order_data(order, order->cursor));
         retro_atomic_store_release_int(&slot->state, 0);
         retro_eventcount_notify(&order->space);
         order->cursor = xe_order_advance(order->cursor);
      }
      previous = retro_atomic_fetch_add_int(&order->publishing, -pending);
      pending = previous - pending;
      if (!pending) return 1;
   }
}
void xe_pipeline_order_reset(xe_pipeline_order *order,
      xe_pipeline_order_sink discard, void *opaque)
{
   size_t i;
   if (!order) return;
   for (i = 0; i < order->capacity; ++i) {
      if (retro_atomic_load_acquire_int(&order->slots[i].state) == 2 && discard)
         discard(opaque, order->payloads + i * order->stride);
      retro_atomic_store_relaxed_int(&order->slots[i].state, 0);
   }
   order->next = order->cursor = 1;
   retro_atomic_store_relaxed_int(&order->publishing, 0);
}
void xe_pipeline_order_free(xe_pipeline_order *order)
{
   if (!order) return;
   retro_eventcount_free(&order->space);
   free(order->payloads);
   free(order->slots);
   free(order);
}
