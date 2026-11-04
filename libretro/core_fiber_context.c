#if !defined(_WIN32)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif
#include "core_fiber_context.h"
#include <assert.h>
#include <string.h>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_CONTEXT_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define XE_CONTEXT_ASAN 1
#endif
#if XE_CONTEXT_ASAN
#include <sanitizer/common_interface_defs.h>
#include <sanitizer/asan_interface.h>
#endif
#define LIBCO_MP
#if defined(_M_AMD64) || defined(__x86_64__) || defined(__amd64__)
#include "../third_party/libretro-common/libco/amd64.c"
#define XE_FIBER_AMD64 1
#elif defined(__aarch64__)
#ifndef HAVE_POSIX_MEMALIGN
#define HAVE_POSIX_MEMALIGN 1
#endif
#include "../third_party/libretro-common/libco/aarch64.c"
#else
#error Unsupported libretro fiber architecture
#endif
#if defined(_MSC_VER)
#define XE_CONTEXT_NOINLINE __declspec(noinline)
#else
#define XE_CONTEXT_NOINLINE __attribute__((noinline))
#endif
static void context_bootstrap(void);
static XE_CONTEXT_NOINLINE void leave_context(void);
#if XE_CONTEXT_ASAN
/* Actual transfer origin is separate from the callback's logical return path. */
static thread_local xe_fiber_context *switch_from;
/* Reload TLS after a context moves to another host thread. */
static XE_CONTEXT_NOINLINE void finish_switch(xe_fiber_context *self)
{
   const void *bottom;
   size_t size;
   xe_fiber_context *previous=switch_from;
   __sanitizer_finish_switch_fiber(self->fake_stack,&bottom,&size);
   switch_from=NULL;
   if(previous && !previous->bottom)
   {
      previous->bottom=(void*)bottom;
      previous->stack_size=size;
   }
}
static void retire_context(xe_fiber_context *context)
{
   xe_fiber_context temporary={0};
   int adopted_temporary=0;
   if(context->adopted || !context->started ||
         (context->finished && !context->fake_stack)) return;
   if(!co_active_handle)
   {
      /* The original native thread may already have exited. */
      xe_fiber_context_adopt(&temporary);
      adopted_temporary=1;
   }
   context->retiring=1;
   context->finished=0;
   xe_fiber_context_switch(context);
   assert(context->finished && !context->fake_stack);
   if(adopted_temporary) xe_fiber_context_detach(&temporary);
}
#endif
int xe_fiber_context_reset(xe_fiber_context *context, void *top, void *bottom,
      void (*entry)(void))
{
   uintptr_t high=(uintptr_t)top, low=(uintptr_t)bottom;
   if(!context || !entry || (high & 15) || high<=low || high-low<4096 ||
         co_active_handle==context->machine || context->adopted)
      return 0;
#if XE_CONTEXT_ASAN
   retire_context(context);
#endif
#if XE_FIBER_AMD64
   memset(context->machine,0,sizeof(context->machine));
   co_reset_context(context->machine,top,bottom,bottom,context_bootstrap);
#else
   co_reset_context(context->machine,top,context_bootstrap);
#endif
   context->entry=entry;
   context->bottom=bottom;
   context->stack_size=high-low;
   context->resumer=NULL;
   context->fake_stack=NULL;
   context->started=context->finished=context->retiring=0;
   return 1;
}
void xe_fiber_context_adopt(xe_fiber_context *context)
{
   memset(context->machine,0,sizeof(context->machine));
   context->entry=NULL;
   context->bottom=NULL;
   context->stack_size=0;
   context->resumer=NULL;
   context->fake_stack=NULL;
   context->started=context->finished=context->retiring=0;
   context->adopted=1;
   co_active_handle=context->machine;
}
void xe_fiber_context_detach(xe_fiber_context *context)
{
   if(co_active_handle==context->machine) co_active_handle=NULL;
}
void xe_fiber_context_destroy(xe_fiber_context *context)
{
   if(co_active_handle==context->machine)
      xe_fiber_context_detach(context);
#if XE_CONTEXT_ASAN
   else retire_context(context);
#endif
   context->fake_stack=NULL;
   context->owner=NULL;
   context->resumer=NULL;
   context->started=0;
   context->finished=1;
}
XE_CONTEXT_NOINLINE void *xe_fiber_context_current_owner(void)
{
   return co_active_handle ? ((xe_fiber_context*)co_active_handle)->owner : NULL;
}
static void context_bootstrap(void)
{
   xe_fiber_context *self=(xe_fiber_context*)co_active_handle;
   self->started=1;
#if XE_CONTEXT_ASAN
   finish_switch(self);
#endif
   self->entry();
   leave_context();
}
static XE_CONTEXT_NOINLINE void leave_context(void)
{
   xe_fiber_context *self=(xe_fiber_context*)co_active_handle;
   xe_fiber_context *next=self->resumer;
   self->finished=1;
   self->fake_stack=NULL;
#if XE_CONTEXT_ASAN
   switch_from=self;
   __asan_handle_no_return();
   __sanitizer_start_switch_fiber(NULL,next->bottom,next->stack_size);
#endif
   co_switch(next->machine);
   assert(0);
}
void xe_fiber_context_switch(xe_fiber_context *context)
{
   xe_fiber_context *previous=(xe_fiber_context*)co_active_handle;
   if(!previous || previous==context || context->finished) return;
   context->resumer=previous;
#if XE_CONTEXT_ASAN
   switch_from=previous;
   __sanitizer_start_switch_fiber(&previous->fake_stack,
         context->bottom,context->stack_size);
#endif
   co_switch(context->machine);
#if XE_CONTEXT_ASAN
   finish_switch(previous);
   /* Retire in this wrapper before resuming any abandoned guest frames. */
   if(previous->retiring) leave_context();
#endif
}
