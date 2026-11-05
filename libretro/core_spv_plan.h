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
/* The allocation contains aligned pointer slots followed by byte flags, one
 * flag per slot. A function reserves its upper bound before borrowing flags;
 * pushes during that walk therefore cannot move the allocation. */
static INLINE int xe_spv_plan_reserve(xe_spv_plan *plan, size_t count)
{
   size_t capacity = plan->heap ? plan->capacity : (size_t)XE_SPV_PLAN_INLINE;
   const size_t stride = sizeof(const void *) + 1;
   const size_t limit = (size_t)-1 / stride;
   const void **nodes;
   if (count <= capacity) return 1;
   if (count > limit) return 0;
   while (capacity < count) {
      if (capacity > limit / 2) { capacity = count; break; }
      capacity *= 2;
   }
   nodes = (const void **)XE_SPV_PLAN_ALLOC(capacity * stride);
   if (!nodes) return 0;
   if (plan->count)
      memcpy(nodes, xe_spv_plan_data(plan), plan->count * sizeof(*nodes));
   XE_SPV_PLAN_FREE(plan->heap);
   plan->heap = nodes;
   plan->capacity = capacity;
   return 1;
}
static INLINE int xe_spv_plan_function(xe_spv_plan *plan, size_t blocks)
{
   if (plan->count == (size_t)-1 || blocks > (size_t)-1 - plan->count - 1)
      return 0;
   return xe_spv_plan_reserve(plan, plan->count + blocks + 1);
}
static INLINE unsigned char *xe_spv_plan_states(xe_spv_plan *plan, size_t count)
{
   if (!plan->heap || count > plan->capacity) return NULL;
   return (unsigned char *)plan->heap + plan->capacity * sizeof(*plan->heap);
}
static INLINE int xe_spv_plan_push(xe_spv_plan *plan, const void *node)
{
   if (plan->count == (size_t)-1 ||
       !xe_spv_plan_reserve(plan, plan->count + 1)) return 0;
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
