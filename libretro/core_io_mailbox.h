#ifndef XE_CORE_IO_MAILBOX_H
#define XE_CORE_IO_MAILBOX_H
#include <queues/mpsc_node.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_io_message {
   mpsc_stack_node_t link;
   void *context;
} xe_io_message;
typedef struct xe_io_mailbox xe_io_mailbox;
/* Cold allocation. Message storage belongs to the caller; no allocation or
 * payload copy occurs on push/take. Only one consumer may take messages. */
xe_io_mailbox *xe_io_mailbox_new(void);
void xe_io_mailbox_push(xe_io_mailbox *box, xe_io_message *message);
xe_io_message *xe_io_mailbox_take(xe_io_mailbox *box);
/* Stop/join producers and consumer, then take/cancel all messages before free.
 * A pushed message remains owned by the mailbox until returned by take. */
void xe_io_mailbox_free(xe_io_mailbox *box);
#ifdef __cplusplus
}
#endif
#endif
