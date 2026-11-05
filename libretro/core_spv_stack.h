/* Single-owner stable frame pages. Growth never relocates/copies live frames.
 * Values are constructed by the typed bridge; only trivial cleanup is allowed. */
#ifndef XE_CORE_SPV_STACK_H
#define XE_CORE_SPV_STACK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#if defined(__SANITIZE_ADDRESS__)
#define XE_SPV_STACK_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_SPV_STACK_ASAN 1
#endif
#endif
#ifdef XE_SPV_STACK_ASAN
#include <sanitizer/asan_interface.h>
#define XE_SPV_STACK_POISON(p,n) __asan_poison_memory_region((p),(n))
#define XE_SPV_STACK_UNPOISON(p,n) __asan_unpoison_memory_region((p),(n))
#else
#define XE_SPV_STACK_POISON(p,n) ((void)0)
#define XE_SPV_STACK_UNPOISON(p,n) ((void)0)
#endif
enum { XE_SPV_STACK_PAGE_FRAMES = 64 };
typedef struct xe_spv_stack_page {
   struct xe_spv_stack_page *next, *previous;
   unsigned char *records;
   size_t used;
} xe_spv_stack_page;
typedef struct xe_spv_stack {
   xe_spv_stack_page *head, *tail, *cursor;
   size_t count, stride;
} xe_spv_stack;
static INLINE int xe_spv_stack_configure(xe_spv_stack *stack, size_t bytes)
{
   size_t stride;
   if (!bytes || bytes > (size_t)-1 - 7) return 0;
   stride = (bytes + 7) & ~(size_t)7;
   if (stride > ((size_t)-1 - sizeof(xe_spv_stack_page) - 63) / XE_SPV_STACK_PAGE_FRAMES) return 0;
   if (stack->stride && stack->stride != stride) return 0;
   stack->stride = stride;
   return 1;
}
static INLINE int xe_spv_stack_grow(xe_spv_stack *stack, void *(*allocate)(size_t))
{
   xe_spv_stack_page *page;
   size_t payload;
   if (!stack->stride) return 0;
   payload = stack->stride * XE_SPV_STACK_PAGE_FRAMES;
   page = (xe_spv_stack_page *)allocate(sizeof(*page) + 63 + payload);
   if (!page) return 0;
   page->next = NULL;
   page->previous = stack->tail;
   page->used = 0;
   page->records = (unsigned char *)(((uintptr_t)(page + 1) + 63) & ~(uintptr_t)63);
   XE_SPV_STACK_POISON(page->records, payload);
   if (stack->tail) stack->tail->next = page;
   else stack->head = page;
   stack->tail = page;
   return 1;
}
static INLINE int xe_spv_stack_reserve(xe_spv_stack *stack, size_t bytes, size_t frames,
      void *(*allocate)(size_t))
{
   xe_spv_stack_page *page;
   size_t present = 0, required = frames / XE_SPV_STACK_PAGE_FRAMES + (frames % XE_SPV_STACK_PAGE_FRAMES != 0);
   if (!xe_spv_stack_configure(stack, bytes)) return 0;
   if (required > (size_t)-1 / (sizeof(xe_spv_stack_page) + 63 + stack->stride * XE_SPV_STACK_PAGE_FRAMES)) return 0;
   for (page = stack->head; page; page = page->next) ++present;
   while (present < required) {
      if (!xe_spv_stack_grow(stack, allocate)) return 0;
      ++present;
   }
   return 1;
}
static INLINE void *xe_spv_stack_push(xe_spv_stack *stack, size_t bytes, void *(*allocate)(size_t))
{
   xe_spv_stack_page *page;
   void *record;
   if (stack->count == (size_t)-1 || !xe_spv_stack_configure(stack, bytes)) return NULL;
   page = stack->cursor ? stack->cursor : stack->head;
   if (page && page->used == XE_SPV_STACK_PAGE_FRAMES) page = page->next;
   if (!page) {
      if (!xe_spv_stack_grow(stack, allocate)) return NULL;
      page = stack->tail;
   }
   record = page->records + page->used * stack->stride;
   XE_SPV_STACK_UNPOISON(record, stack->stride);
   ++page->used;
   ++stack->count;
   stack->cursor = page;
   return record;
}
static INLINE void *xe_spv_stack_top(const xe_spv_stack *stack)
{
   return stack->count ? stack->cursor->records + (stack->cursor->used - 1) * stack->stride : NULL;
}
static INLINE int xe_spv_stack_pop(xe_spv_stack *stack)
{
   xe_spv_stack_page *page = stack->cursor;
   if (!stack->count) return 0;
   --page->used;
   --stack->count;
   XE_SPV_STACK_POISON(page->records + page->used * stack->stride, stack->stride);
   if (!page->used && page->previous) stack->cursor = page->previous;
   return 1;
}
static INLINE void xe_spv_stack_reset(xe_spv_stack *stack)
{
   xe_spv_stack_page *page;
   if (!stack->count) return;
   for (page = stack->head; page; page = page->next) {
      XE_SPV_STACK_POISON(page->records, page->used * stack->stride);
      page->used = 0;
   }
   stack->count = 0;
   stack->cursor = stack->head;
}
static INLINE void xe_spv_stack_close(xe_spv_stack *stack, void (*release)(void *))
{
   xe_spv_stack_page *page, *next;
   for (page = stack->head; page; page = next) {
      next = page->next;
      XE_SPV_STACK_UNPOISON(page->records, stack->stride * XE_SPV_STACK_PAGE_FRAMES);
      release(page);
   }
   memset(stack, 0, sizeof(*stack));
}
#endif
