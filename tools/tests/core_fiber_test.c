#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* Actual Fiber wrapper: stable stack reuse, return handoff and adoption. */
#include <stdint.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#endif
#define CHECK(test) do { if(!(test)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while(0)
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XE_TEST_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#ifndef XE_TEST_ASAN
#define XE_TEST_ASAN 1
#endif
#endif
#if XE_TEST_ASAN
#include <sanitizer/asan_interface.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#endif
static void *last_fake_stack;
static unsigned retired_fake_stacks;
static void check_retired_fake(void)
{
   if(!last_fake_stack) return;
#ifdef _WIN32
   {
      static MEMORY_BASIC_INFORMATION info;
      CHECK(VirtualQuery(last_fake_stack,&info,sizeof(info))==sizeof(info));
      CHECK(info.State==MEM_FREE);
   }
#else
   {
      static unsigned char resident;
      long page=sysconf(_SC_PAGESIZE);
      uintptr_t address;
      CHECK(page>0);
      address=(uintptr_t)last_fake_stack & ~((uintptr_t)page-1);
      CHECK(mincore((void*)address,(size_t)page,&resident)==-1 && errno==ENOMEM);
   }
#endif
   ++retired_fake_stacks;
}
#else
#define check_retired_fake() ((void)0)
#endif
extern void *xe_test_fiber_adopt(void);
extern void *xe_test_fiber_create(void (*entry)(void));
extern void xe_test_fiber_switch(void *);
extern void xe_test_fiber_restart(void *);
extern void xe_test_fiber_delete(void *);
extern void *xe_test_fiber_current(void);
extern void xe_test_fiber_shutdown(void);
static void *parent,*child,*a,*b;
static uintptr_t first_stack;
static unsigned entries,visits,a_done,b_done;
static void entry(void)
{
   volatile unsigned canary=0x12345678U;
   unsigned i;
#ifdef _MSC_VER
   uintptr_t stack=(uintptr_t)_AddressOfReturnAddress();
#else
   uintptr_t stack=(uintptr_t)__builtin_frame_address(0);
#endif
   if(!first_stack) first_stack=stack;
   CHECK(first_stack==stack);
   CHECK(xe_test_fiber_current()==child);
#if XE_TEST_ASAN
   last_fake_stack=__asan_get_current_fake_stack();
#endif
   ++entries;
   for(i=0;i<10;++i)
   {
      CHECK(canary==0x12345678U);
      ++visits;
      xe_test_fiber_switch(parent);
      CHECK(xe_test_fiber_current()==child);
   }
}
static void immediate_entry(void)
{
   volatile unsigned canary=0x13579bdfU;
   CHECK(canary==0x13579bdfU);
#if XE_TEST_ASAN
   last_fake_stack=__asan_get_current_fake_stack();
#endif
}
static void b_entry(void)
{
   xe_test_fiber_switch(parent);
   CHECK(xe_test_fiber_current()==b);
   ++b_done;
}
static void a_entry(void)
{
   xe_test_fiber_switch(b);
   CHECK(xe_test_fiber_current()==a);
   ++a_done;
}
static void *orphan_parent,*orphan_child;
static unsigned orphan_visits;
static void orphan_entry(void)
{
   volatile unsigned canary=0x76543210U;
#if XE_TEST_ASAN
   last_fake_stack=__asan_get_current_fake_stack();
#endif
   ++orphan_visits;
   for(;;)
   {
      CHECK(canary==0x76543210U);
      xe_test_fiber_switch(orphan_parent);
      CHECK(xe_test_fiber_current()==orphan_child);
      ++orphan_visits;
   }
}
static void orphan_worker(void *unused)
{
   (void)unused;
   orphan_parent=xe_test_fiber_adopt(); CHECK(orphan_parent);
   orphan_child=xe_test_fiber_create(orphan_entry); CHECK(orphan_child);
   xe_test_fiber_switch(orphan_child);
   CHECK(orphan_visits==1);
   xe_test_fiber_delete(orphan_parent);
   CHECK(xe_test_fiber_current()==NULL);
}
#if defined(_MSC_VER)
#define XE_TEST_NOINLINE __declspec(noinline)
#define XE_TEST_TLS __declspec(thread)
#else
#define XE_TEST_NOINLINE __attribute__((noinline))
#define XE_TEST_TLS __thread
#endif
static unsigned deep_visits;
static XE_TEST_NOINLINE void deep_probe(unsigned depth)
{
   volatile unsigned char page[4096];
   page[0]=(unsigned char)depth;
   page[4095]=(unsigned char)(depth^0xa5U);
   if(depth) deep_probe(depth-1);
   else xe_test_fiber_switch(parent);
   CHECK(xe_test_fiber_current()==child);
   CHECK(page[0]==(unsigned char)depth);
   CHECK(page[4095]==(unsigned char)(depth^0xa5U));
   ++deep_visits;
}
static void deep_entry(void)
{
   deep_probe(80);
}
struct worker_context
{
   void *parent;
   void *child;
   unsigned visits;
};
static XE_TEST_TLS struct worker_context *current_worker;
static void concurrent_entry(void)
{
   struct worker_context *state=current_worker;
   volatile unsigned canary=0x91ac43e7U;
   unsigned i;
   for(i=0;i<16;++i)
   {
      CHECK(current_worker==state && canary==0x91ac43e7U);
      CHECK(xe_test_fiber_current()==state->child);
      ++state->visits;
      xe_test_fiber_switch(state->parent);
   }
}
static void concurrent_worker(void *unused)
{
   struct worker_context state;
   unsigned round,i;
   (void)unused;
   state.visits=0;
   current_worker=&state;
   state.parent=xe_test_fiber_adopt(); CHECK(state.parent);
   state.child=xe_test_fiber_create(concurrent_entry); CHECK(state.child);
   for(round=0;round<500;++round)
   {
      if(round) xe_test_fiber_restart(state.child);
      for(i=0;i<17;++i)
      {
         xe_test_fiber_switch(state.child);
         CHECK(xe_test_fiber_current()==state.parent);
      }
   }
   CHECK(state.visits==8000);
   xe_test_fiber_delete(state.child);
   xe_test_fiber_delete(state.parent);
   CHECK(xe_test_fiber_current()==NULL);
   current_worker=NULL;
}
int main(void)
{
   unsigned round,i;
   sthread_t *worker;
   sthread_t *workers[8];
#ifdef _WIN32
   SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif
   parent=xe_test_fiber_adopt(); CHECK(parent);
   child=xe_test_fiber_create(entry); CHECK(child);
   for(round=0;round<2000;++round)
   {
      if(round) xe_test_fiber_restart(child);
      for(i=0;i<11;++i)
      {
         xe_test_fiber_switch(child);
         CHECK(xe_test_fiber_current()==parent);
      }
      check_retired_fake();
   }
   CHECK(entries==2000 && visits==20000);
   for(round=0;round<1000;++round)
   {
      xe_test_fiber_restart(child);
      check_retired_fake();
      xe_test_fiber_switch(child);
      CHECK(xe_test_fiber_current()==parent);
   }
   CHECK(entries==3000 && visits==21000);
   xe_test_fiber_delete(child);
   check_retired_fake();
   for(round=0;round<64;++round)
   {
      child=xe_test_fiber_create(immediate_entry); CHECK(child);
      xe_test_fiber_switch(child);
      CHECK(xe_test_fiber_current()==parent);
      xe_test_fiber_delete(child);
      check_retired_fake();
   }
   child=xe_test_fiber_create(deep_entry); CHECK(child);
   xe_test_fiber_switch(child);
   CHECK(deep_visits==0 && xe_test_fiber_current()==parent);
   xe_test_fiber_switch(child);
   CHECK(deep_visits==81 && xe_test_fiber_current()==parent);
   xe_test_fiber_delete(child);
   b=xe_test_fiber_create(b_entry); a=xe_test_fiber_create(a_entry);
   CHECK(a && b);
   xe_test_fiber_switch(b);
   xe_test_fiber_switch(a);
   CHECK(a_done==1 && b_done==1 && xe_test_fiber_current()==parent);
   xe_test_fiber_delete(a); xe_test_fiber_delete(b);
   xe_test_fiber_delete(parent);
   CHECK(xe_test_fiber_current()==NULL);
   parent=xe_test_fiber_adopt(); CHECK(parent);
   xe_test_fiber_delete(parent);
   for(i=0;i<3;++i)
   {
      orphan_visits=0;
      worker=sthread_create(orphan_worker,NULL); CHECK(worker);
      sthread_join(worker);
      CHECK(xe_test_fiber_current()==NULL && orphan_visits==1);
      if(i==2)
      {
         orphan_parent=xe_test_fiber_adopt(); CHECK(orphan_parent);
         xe_test_fiber_switch(orphan_child);
         CHECK(orphan_visits==2 && xe_test_fiber_current()==orphan_parent);
         xe_test_fiber_delete(orphan_parent);
      }
      if(i==0)
      {
         xe_test_fiber_restart(orphan_child);
         check_retired_fake();
      }
      xe_test_fiber_delete(orphan_child);
      check_retired_fake();
      CHECK(orphan_visits==(i==2 ? 2U : 1U) && xe_test_fiber_current()==NULL);
   }
   for(i=0;i<8;++i)
   {
      workers[i]=sthread_create(concurrent_worker,NULL);
      CHECK(workers[i]);
   }
   for(i=0;i<8;++i) sthread_join(workers[i]);
   xe_test_fiber_shutdown();
#if XE_TEST_ASAN
   printf("Retired fake-stack mappings checked: %u\n",retired_fake_stacks);
#endif
   puts("Fiber wrapper: 3000 reused-stack executions including suspended restart, return resumer and readoption: PASS");
   puts("Fiber wrapper: eight concurrent workers, 4000 executions and 64000 yields: PASS");
   return 0;
}
