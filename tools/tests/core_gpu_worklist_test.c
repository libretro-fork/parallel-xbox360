#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "libretro/core_gpu_worklist.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
static unsigned allocations, frees;
static int fail_allocation;
static void *tracked_malloc(size_t bytes)
{
   ++allocations;
   return fail_allocation ? NULL : malloc(bytes);
}
static void tracked_free(void *pointer)
{
   if (pointer) ++frees;
   free(pointer);
}
/* Count every allocator call in the actual C implementation, test only. */
#define malloc tracked_malloc
#define free tracked_free
#include "libretro/core_gpu_worklist.c"
#undef malloc
#undef free
struct record { const void *borrowed; uint64_t value; };
int main(void)
{
   xe_gpu_worklist list = {0};
   struct record *record;
   void *storage;
   size_t i;
   unsigned pass;
   CHECK(!xe_gpu_worklist_init(NULL,1,1));
   CHECK(!xe_gpu_worklist_init(&list,0,1));
   CHECK(!xe_gpu_worklist_init(&list,1,0));
   CHECK(!xe_gpu_worklist_init(&list,(size_t)-1,2));
   CHECK(!allocations);
   fail_allocation = 1;
   CHECK(!xe_gpu_worklist_init(&list,257,sizeof(*record)));
   CHECK(!list.data && !list.count);
   fail_allocation = 0;
   CHECK(xe_gpu_worklist_init(&list,257,sizeof(*record)));
   storage = list.data;
   for (i=0;i<257*sizeof(*record);++i)
      CHECK(((const unsigned char *)storage)[i] == 0);
   CHECK(xe_gpu_worklist_init(&list,257,sizeof(*record)));
   CHECK(!xe_gpu_worklist_init(&list,258,sizeof(*record)));
   CHECK(!xe_gpu_worklist_init(&list,257,sizeof(*record)+1));
   CHECK(allocations == 2);
   for (pass=0;pass<10000;++pass) {
      xe_gpu_worklist_reset(&list);
      CHECK(!list.failed && !list.count && list.data == storage);
      for (i=0;i<257;++i) {
         record = (struct record *)xe_gpu_worklist_append(&list);
         CHECK(record == (struct record *)storage+i);
         record->borrowed = storage;
         record->value = i;
      }
      CHECK(!xe_gpu_worklist_append(&list));
      CHECK(list.failed && list.count == 257 && allocations == 2 && !frees);
      for (i=0;i<257;++i) {
         record = (struct record *)storage+i;
         CHECK(record->borrowed == storage && record->value == i);
      }
   }
   xe_gpu_worklist_close(&list);
   CHECK(!list.data && !list.count && !list.capacity && !list.stride && !list.failed);
   xe_gpu_worklist_close(&list);
   CHECK(frees == 1);
   CHECK(!xe_gpu_worklist_append(&list));
   xe_gpu_worklist_reset(&list);
   CHECK(xe_gpu_worklist_init(&list,1,sizeof(*record)));
   xe_gpu_worklist_close(&list);
   CHECK(allocations == 3 && frees == 2);
   puts("PASS: 2.57 million fixed-slot appends, no hot allocator calls, borrowed pointer identity, exhaustion, reset and failed initialization");
   return 0;
}
