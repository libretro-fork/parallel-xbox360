#ifndef XE_LIBRETRO_CORE_FIBER_CONTEXT_H
#define XE_LIBRETRO_CORE_FIBER_CONTEXT_H
#include <stdint.h>
#include <stddef.h>
#include <retro_common_api.h>
#if defined(_MSC_VER)
#define XE_FIBER_ALIGN __declspec(align(16))
#else
#define XE_FIBER_ALIGN __attribute__((aligned(16)))
#endif
typedef struct XE_FIBER_ALIGN xe_fiber_context
{
   /* Keep machine first: libco's active handle also identifies this record. */
#if defined(_WIN32)
   uint64_t machine[34];
#elif defined(__aarch64__)
   uint64_t machine[23];
#else
   uint64_t machine[8];
#endif
   unsigned finished, started, retiring, adopted;
   void *owner;
   struct xe_fiber_context *resumer;
   void *bottom;
   size_t stack_size;
   void *fake_stack;
   void (*entry)(void);
} xe_fiber_context;
RETRO_BEGIN_DECLS
/* Zero-initialize the record before its first use. */
int xe_fiber_context_reset(xe_fiber_context *context, void *top, void *bottom,
      void (*entry)(void));
void xe_fiber_context_adopt(xe_fiber_context *context);
void xe_fiber_context_detach(xe_fiber_context *context);
void xe_fiber_context_destroy(xe_fiber_context *context);
void *xe_fiber_context_current_owner(void);
void xe_fiber_context_switch(xe_fiber_context *context);
RETRO_END_DECLS
#undef XE_FIBER_ALIGN
#endif
