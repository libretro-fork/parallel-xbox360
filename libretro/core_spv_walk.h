/* Single-traversal CFG bookkeeping. Nodes and edges stay borrowed and immutable.
 * Callbacks must return failure instead of throwing through the C boundary. */
#ifndef XE_CORE_SPV_WALK_H
#define XE_CORE_SPV_WALK_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_SPV_WALK_ALLOC
#define XE_SPV_WALK_ALLOC malloc
#define XE_SPV_WALK_FREE free
#endif
enum { XE_SPV_WALK_CONTROL = 0, XE_SPV_WALK_DEAD_CONTINUE = 1,
       XE_SPV_WALK_DEAD_MERGE = 2, XE_SPV_WALK_INLINE = 256 };
typedef struct xe_spv_walk_ops {
   size_t (*index)(void *context, const void *node);
   size_t (*successors)(const void *node);
   const void *(*successor)(const void *node, size_t index);
   int (*targets)(const void *node, const void **merge, const void **continuation);
   int (*visit)(void *context, const void *node, int reason, const void *header);
} xe_spv_walk_ops;
typedef struct xe_spv_walk_state {
   const xe_spv_walk_ops *ops;
   void *context;
   unsigned char *states;
   size_t count;
} xe_spv_walk_state;
static INLINE int xe_spv_walk_visit(xe_spv_walk_state *walk, const void *node,
      int reason, const void *header)
{
   size_t index, i, successors, target_index;
   const void *merge = NULL, *continuation = NULL;
   int target_reason;
   if (!node) return 0;
   index = walk->ops->index(walk->context, node);
   if (index >= walk->count) return 0;
   if (reason == XE_SPV_WALK_CONTROL) walk->states[index] |= 4;
   if (walk->states[index] & 3) return 1; /* visited or delayed */
   if (!walk->ops->visit(walk->context, node, reason, header)) return 0;
   walk->states[index] |= 1;
   if (!walk->ops->targets(node, &merge, &continuation)) return 0;
   if (merge) {
      target_index = walk->ops->index(walk->context, merge);
      if (target_index >= walk->count) return 0;
      walk->states[target_index] |= 2;
   }
   if (continuation) {
      target_index = walk->ops->index(walk->context, continuation);
      if (target_index >= walk->count) return 0;
      walk->states[target_index] |= 2;
   }
   if (reason == XE_SPV_WALK_CONTROL) {
      successors = walk->ops->successors(node);
      for (i = 0; i < successors; ++i)
         if (!xe_spv_walk_visit(walk, walk->ops->successor(node, i), reason, NULL)) return 0;
   }
   if (continuation) {
      target_index = walk->ops->index(walk->context, continuation);
      target_reason = (walk->states[target_index] & 4) ? XE_SPV_WALK_CONTROL : XE_SPV_WALK_DEAD_CONTINUE;
      walk->states[target_index] = (unsigned char)(walk->states[target_index] & 0xFDu);
      if (!xe_spv_walk_visit(walk, continuation, target_reason, node)) return 0;
   }
   if (merge) {
      target_index = walk->ops->index(walk->context, merge);
      target_reason = (walk->states[target_index] & 4) ? XE_SPV_WALK_CONTROL : XE_SPV_WALK_DEAD_MERGE;
      walk->states[target_index] = (unsigned char)(walk->states[target_index] & 0xFDu);
      if (!xe_spv_walk_visit(walk, merge, target_reason, node)) return 0;
   }
   return 1;
}
/* Borrowed scratch is exclusive and must remain fixed for the whole walk. */
static INLINE int xe_spv_walk_graph_states(const xe_spv_walk_ops *ops, void *context,
      const void *root, size_t count, unsigned char *states, size_t capacity)
{
   xe_spv_walk_state walk;
   if (!states || count > capacity || !count || !root || !ops ||
       !ops->index || !ops->successors || !ops->successor ||
       !ops->targets || !ops->visit) return 0;
   memset(states, 0, count);
   walk.ops = ops;
   walk.context = context;
   walk.states = states;
   walk.count = count;
   return xe_spv_walk_visit(&walk, root, XE_SPV_WALK_CONTROL, NULL);
}
static INLINE int xe_spv_walk_graph(const xe_spv_walk_ops *ops, void *context,
      const void *root, size_t count)
{
   unsigned char local[XE_SPV_WALK_INLINE], *states = local;
   int result;
   if (!count || !root || !ops || !ops->index || !ops->successors ||
       !ops->successor || !ops->targets || !ops->visit) return 0;
   if (count > sizeof(local)) {
      states = (unsigned char *)XE_SPV_WALK_ALLOC(count);
      if (!states) return 0;
   }
   result = xe_spv_walk_graph_states(ops, context, root, count, states, count);
   if (states != local) XE_SPV_WALK_FREE(states);
   return result;
}
#endif
