#ifndef XE_CORE_PIPELINE_ORDER_H
#define XE_CORE_PIPELINE_ORDER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_pipeline_order xe_pipeline_order;
typedef void (*xe_pipeline_order_sink)(void *opaque, void *payload);
/* Single reservation producer, multiple completion workers. Reserve before
 * enqueue, outside any lock workers need. Callbacks are serialized, must not
 * reenter this order, and own construction/destruction of payload objects.
 * Each ticket has exactly one completion worker. */
xe_pipeline_order *xe_pipeline_order_new(size_t capacity, size_t payload_size,
      xe_pipeline_order_sink publish, void *opaque);
size_t xe_pipeline_order_reserve(xe_pipeline_order *order);
void *xe_pipeline_order_payload(xe_pipeline_order *order, size_t ticket);
int xe_pipeline_order_complete(xe_pipeline_order *order, size_t ticket);
/* All producers/workers must be joined. Discard only constructed completions,
 * including those behind an unfinished request. Then restart ticket numbering. */
void xe_pipeline_order_reset(xe_pipeline_order *order,
      xe_pipeline_order_sink discard, void *opaque);
/* Free after reset has destroyed any constructed payload objects. */
void xe_pipeline_order_free(xe_pipeline_order *order);
#ifdef __cplusplus
}
#endif
#endif
