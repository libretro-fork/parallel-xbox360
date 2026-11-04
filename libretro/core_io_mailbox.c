#include "core_io_mailbox.h"
#include <queues/mpsc_stack.h>
#include <stdlib.h>
#if !defined(RETRO_ATOMIC_HAS_PTR)
#error The guest I/O mailbox requires pointer atomics
#endif
struct xe_io_mailbox {
   mpsc_stack_t incoming;
   mpsc_stack_node_t *pending;
};
xe_io_mailbox *xe_io_mailbox_new(void) {
   xe_io_mailbox *box = (xe_io_mailbox *)malloc(sizeof(*box));
   if (box) { mpsc_stack_init(&box->incoming); box->pending = NULL; }
   return box;
}
void xe_io_mailbox_push(xe_io_mailbox *box, xe_io_message *message) {
   mpsc_stack_push(&box->incoming, &message->link);
}
xe_io_message *xe_io_mailbox_take(xe_io_mailbox *box) {
   mpsc_stack_node_t *node;
   if (!box->pending)
      box->pending = mpsc_stack_reverse(mpsc_stack_drain(&box->incoming));
   node = box->pending;
   if (!node) return NULL;
   box->pending = node->next;
   /* link is the first member of the standard-layout C message. */
   return (xe_io_message *)node;
}
void xe_io_mailbox_free(xe_io_mailbox *box) { free(box); }
