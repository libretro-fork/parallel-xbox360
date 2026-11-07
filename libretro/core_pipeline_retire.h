/* MSVC C89. GPU owner reserves/cancels/collects. One creator publishes each
 * reserved slot once; it must stop touching the slot after publishing it.
 * No allocation, wait or lock after creation. Handles remain externally owned
 * until publish transfers them. Shutdown requires joined creators + idle GPU. */
#ifndef XE_CORE_PIPELINE_RETIRE_H
#define XE_CORE_PIPELINE_RETIRE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_pipeline_retire_pool xe_pipeline_retire_pool;
typedef struct xe_pipeline_retire_slot xe_pipeline_retire_slot;
typedef void (*xe_pipeline_retire_destroy)(void *, uint64_t);
xe_pipeline_retire_pool *xe_pipeline_retire_new(size_t count);
size_t xe_pipeline_retire_available(const xe_pipeline_retire_pool *pool);
xe_pipeline_retire_slot *xe_pipeline_retire_reserve(xe_pipeline_retire_pool *pool);
/* Owner only, before handing the slot to a creator. */
void xe_pipeline_retire_cancel(xe_pipeline_retire_slot *slot);
/* Creator only. A zero handle returns unused metadata without destroying the
 * still-live placeholder. Its existing pipeline entry remains the owner. */
void xe_pipeline_retire_publish(xe_pipeline_retire_slot *slot,
      uint64_t handle, uint64_t submission);
/* Consume the entry's reservation before publication; repeated transfers are
 * rejected without touching a slot the consumer may already have recycled. */
int xe_pipeline_retire_transfer(xe_pipeline_retire_slot **owned,
      uint64_t handle, uint64_t submission);
size_t xe_pipeline_retire_collect(xe_pipeline_retire_pool *pool,
      uint64_t completed, size_t budget, xe_pipeline_retire_destroy destroy,
      void *opaque);
/* Joined creators and idle GPU only. Reserved/unpublished handles still
 * belong to the renderer's entries; only published handles are destroyed.
 * Returns0 and retains the pool if a needed destructor is missing. Destruction
 * callbacks must not reenter this pool. Reserved slot pointers expire on free. */
int xe_pipeline_retire_free(xe_pipeline_retire_pool *pool,
      xe_pipeline_retire_destroy destroy, void *opaque);
#ifdef __cplusplus
}
#endif
#endif
