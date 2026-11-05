/* Exclusive instruction ownership. Payloads supply their actual void* link
 * slot; no pointer-to-pointer casts or container-offset recovery are needed. */
#ifndef XE_CORE_SPV_INSTRUCTION_CHAIN_H
#define XE_CORE_SPV_INSTRUCTION_CHAIN_H
#include <retro_inline.h>
#ifdef __cplusplus
typedef void **(*xe_spv_next_slot_fn)(void *) noexcept;
typedef void (*xe_spv_instruction_destroy_fn)(void *) noexcept;
#define XE_SPV_INSTRUCTION_NOTHROW noexcept
#else
typedef void **(*xe_spv_next_slot_fn)(void *);
typedef void (*xe_spv_instruction_destroy_fn)(void *);
#define XE_SPV_INSTRUCTION_NOTHROW
#endif
typedef struct xe_spv_instruction_chain { void *head, *tail; } xe_spv_instruction_chain;
typedef struct xe_spv_instruction_body {
   xe_spv_instruction_chain chain;
   void *penultimate;
} xe_spv_instruction_body;
/* Caller owns an unlinked record exclusively, and releases that ownership
 * after adoption. Labels use their slot for their block, and cannot be linked. */
static INLINE void xe_spv_instruction_append(xe_spv_instruction_chain *chain,
      void *record, xe_spv_next_slot_fn slot) XE_SPV_INSTRUCTION_NOTHROW
{
   if (chain->tail) *slot(chain->tail) = record;
   else chain->head = record;
   chain->tail = record;
}
static INLINE void xe_spv_instruction_body_append(xe_spv_instruction_body *body,
      void *record, xe_spv_next_slot_fn slot) XE_SPV_INSTRUCTION_NOTHROW
{
   body->penultimate = body->chain.tail;
   xe_spv_instruction_append(&body->chain, record, slot);
}
/* previous/target are consecutive borrowed nodes (previous null at head).
 * The caller retains these while visiting; insertion does not invalidate them. */
static INLINE int xe_spv_instruction_insert(xe_spv_instruction_body *body,
      void *previous, void *target, void *record, xe_spv_next_slot_fn slot)
      XE_SPV_INSTRUCTION_NOTHROW
{
   void **before = previous ? slot(previous) : &body->chain.head;
   if (!target || *before != target) return 0;
   *slot(record) = target;
   *before = record;
   if (body->chain.tail == target) body->penultimate = record;
   return 1;
}
/* Terminal walk, including freeing callbacks: read next before destruction.
 * Reinitialize the descriptor if it will be reused after this call. */
static INLINE void xe_spv_instruction_destroy(void *head,
      xe_spv_next_slot_fn slot, xe_spv_instruction_destroy_fn destroy)
      XE_SPV_INSTRUCTION_NOTHROW
{
   void *next;
   while (head) {
      next = *slot(head);
      destroy(head);
      head = next;
   }
}
#undef XE_SPV_INSTRUCTION_NOTHROW
#endif
