/* Exclusive append-only ownership links. The typed caller owns payload cleanup.
 * A node may belong to one chain; the terminal walk consumes its payload. */
#ifndef XE_CORE_SPV_BLOCK_CHAIN_H
#define XE_CORE_SPV_BLOCK_CHAIN_H
#include <stddef.h>
#include <retro_inline.h>
typedef struct xe_spv_block_link { struct xe_spv_block_link *next; } xe_spv_block_link;
typedef struct xe_spv_block_chain {
   xe_spv_block_link *head, *tail;
   size_t count;
} xe_spv_block_chain;
#ifdef __cplusplus
typedef void (*xe_spv_block_destroy_fn)(xe_spv_block_link *) noexcept;
#define XE_SPV_CHAIN_NOTHROW noexcept
#else
typedef void (*xe_spv_block_destroy_fn)(xe_spv_block_link *);
#define XE_SPV_CHAIN_NOTHROW
#endif
static INLINE int xe_spv_block_chain_append(xe_spv_block_chain *chain,
      xe_spv_block_link *node)
{
   if (!node || node->next || node == chain->tail || chain->count == (size_t)-1)
      return 0;
   if (chain->tail) chain->tail->next = node;
   else chain->head = node;
   chain->tail = node;
   ++chain->count;
   return 1;
}
/* Terminal ownership walk: cache next before destruction. The chain is not
 * accessed again unless reinitialized; callback must not throw or mutate it. */
static INLINE void xe_spv_block_chain_destroy(const xe_spv_block_chain *chain,
      xe_spv_block_destroy_fn destroy) XE_SPV_CHAIN_NOTHROW
{
   xe_spv_block_link *node = chain->head, *next;
   while (node) {
      next = node->next;
      destroy(node);
      node = next;
   }
}
#undef XE_SPV_CHAIN_NOTHROW
#endif
