/* Borrowed CFG emission order. Exclusive owner; graph stays immutable until
 * replay ends. Reset invalidates views and retains capacity for the next shader. */
#ifndef XE_CORE_SPV_PLAN_H
#define XE_CORE_SPV_PLAN_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_SPV_PLAN_ALLOC
#define XE_SPV_PLAN_ALLOC malloc
#define XE_SPV_PLAN_FREE free
#endif
enum { XE_SPV_PLAN_INLINE = 64 };
typedef struct xe_spv_plan {
   const void **heap;
   size_t count, capacity, cursor;
   const void *local[XE_SPV_PLAN_INLINE];
} xe_spv_plan;
static INLINE const void **xe_spv_plan_data(xe_spv_plan *plan)
{
   return plan->heap ? plan->heap : plan->local;
}
static INLINE void xe_spv_plan_reset(xe_spv_plan *plan)
{
   plan->count = plan->cursor = 0;
}
static INLINE void xe_spv_plan_close(xe_spv_plan *plan)
{
   XE_SPV_PLAN_FREE(plan->heap);
   memset(plan, 0, sizeof(*plan));
}
static INLINE int xe_spv_plan_push(xe_spv_plan *plan, const void *node)
{
   size_t capacity = plan->heap ? plan->capacity : XE_SPV_PLAN_INLINE;
   const void **nodes;
   if (plan->count == capacity) {
      if (capacity > (size_t)-1 / sizeof(*nodes) / 2) return 0;
      capacity *= 2;
      nodes = (const void **)XE_SPV_PLAN_ALLOC(capacity * sizeof(*nodes));
      if (!nodes) return 0;
      memcpy(nodes, xe_spv_plan_data(plan), plan->count * sizeof(*nodes));
      XE_SPV_PLAN_FREE(plan->heap);
      plan->heap = nodes;
      plan->capacity = capacity;
   }
   xe_spv_plan_data(plan)[plan->count++] = node;
   return 1;
}
static INLINE int xe_spv_plan_next(xe_spv_plan *plan, const void **node)
{
   if (plan->cursor >= plan->count) return 0;
   *node = xe_spv_plan_data(plan)[plan->cursor++];
   return 1;
}
#endif
