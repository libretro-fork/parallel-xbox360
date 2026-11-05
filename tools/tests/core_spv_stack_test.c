#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_spv_stack.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static size_t allocations, releases;
static int fail_allocation;
static void *allocate(size_t bytes) { if(fail_allocation) return NULL; ++allocations; return malloc(bytes); }
static void release(void *value) { if(value) ++releases; free(value); }
int main(int argc, char **argv)
{
   xe_spv_stack stack;
   uint32_t *frames[1024], *frame;
   size_t i, pass, warmed;
   memset(&stack,0,sizeof(stack));
   CHECK(!xe_spv_stack_top(&stack) && !xe_spv_stack_pop(&stack));
   CHECK(!xe_spv_stack_reserve(&stack,0,64,allocate));
   CHECK(!xe_spv_stack_reserve(&stack,(size_t)-1,64,allocate));
   CHECK(!xe_spv_stack_reserve(&stack,32,(size_t)-1,allocate));
   CHECK(xe_spv_stack_reserve(&stack,32,64,allocate));
   for(i=0;i<64;++i) {
      frames[i]=(uint32_t*)xe_spv_stack_push(&stack,32,allocate);
      CHECK(frames[i]); frames[i][0]=(uint32_t)i;
   }
   CHECK((uintptr_t)frames[0]%64==0);
   fail_allocation=1;
   CHECK(!xe_spv_stack_push(&stack,32,allocate));
   CHECK(stack.count==64 && xe_spv_stack_top(&stack)==frames[63]);
   fail_allocation=0;
   for(i=64;i<1024;++i) {
      frames[i]=(uint32_t*)xe_spv_stack_push(&stack,32,allocate);
      CHECK(frames[i]); frames[i][0]=(uint32_t)i;
   }
   for(i=0;i<1024;++i) CHECK(frames[i][0]==i);
   CHECK(!xe_spv_stack_push(&stack,64,allocate));
   warmed=allocations;
   for(pass=0;pass<100;++pass) {
      for(i=1024;i;--i) {
         frame=(uint32_t*)xe_spv_stack_top(&stack);
         CHECK(frame==frames[i-1] && frame[0]==i-1);
         CHECK(xe_spv_stack_pop(&stack));
      }
      CHECK(!xe_spv_stack_top(&stack));
      for(i=0;i<1024;++i) {
         frame=(uint32_t*)xe_spv_stack_push(&stack,32,allocate);
         CHECK(frame==frames[i]); frame[0]=(uint32_t)i;
      }
   }
   CHECK(allocations==warmed);
   if(argc>1) {
      volatile uint32_t read;
      frame=(uint32_t*)xe_spv_stack_top(&stack);
      if(!strcmp(argv[1],"pop-uaf")) CHECK(xe_spv_stack_pop(&stack));
      else if(!strcmp(argv[1],"reset-uaf")) xe_spv_stack_reset(&stack);
      else return 2;
      read=frame[0]; (void)read;
   }
   xe_spv_stack_reset(&stack);
   CHECK(!stack.count && !xe_spv_stack_top(&stack));
   CHECK(xe_spv_stack_push(&stack,32,allocate)==frames[0]);
   xe_spv_stack_close(&stack,release);
   CHECK(allocations==releases && !stack.head);
   puts("PASS: 1024 stable frames, 102400 warm push/pop pairs, growth failure/retry, overflow and reset cleanup");
   return 0;
}
