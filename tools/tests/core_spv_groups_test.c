#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_spv_groups.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static size_t allocations, releases;
static int fail_allocation;
static void *allocate(size_t bytes) { if(fail_allocation) return NULL; ++allocations; return malloc(bytes); }
static void release(void *value) { if(value) ++releases; free(value); }
int main(void)
{
   xe_spv_groups groups;
   xe_spv_record_view view, borrowed;
   xe_spv_group *group;
   unsigned records[64];
   size_t i, j, pass, calls;
   memset(&groups,0,sizeof(groups));
   CHECK(!xe_spv_groups_view(&groups,0).count);
   fail_allocation=1;
   CHECK(!xe_spv_groups_append(&groups,0,records,allocate,release));
   CHECK(!groups.count && !groups.capacity);
   fail_allocation=0;
   for(i=0;i<4;++i) CHECK(xe_spv_groups_append(&groups,0,records+i,allocate,release));
   borrowed=xe_spv_groups_view(&groups,0);
   fail_allocation=1;
   CHECK(!xe_spv_groups_append(&groups,0,records+4,allocate,release));
   view=xe_spv_groups_view(&groups,0);
   CHECK(view.count==4 && view.records==borrowed.records);
   CHECK(!xe_spv_groups_reserve(&groups,(size_t)-1,allocate,release));
   group=xe_spv_groups_find(&groups,0);
   CHECK(!xe_spv_group_reserve(group,(size_t)-1,allocate,release));
   fail_allocation=0;
   /* Descriptor growth must not relocate another group's borrowed array. */
   for(i=1;i<128;++i) CHECK(xe_spv_groups_append(&groups,(uint32_t)i,records,allocate,release));
   view=xe_spv_groups_view(&groups,0);
   CHECK(view.records==borrowed.records && view.count==4);
   for(i=0;i<4;++i) CHECK(view.records[i]==records+i);
   for(i=0;i<128;++i)
      for(j=0;j<32;++j) CHECK(xe_spv_groups_append(&groups,(uint32_t)i,records+j,allocate,release));
   calls=allocations;
   for(pass=0;pass<100;++pass) {
      xe_spv_groups_reset(&groups);
      CHECK(!xe_spv_groups_view(&groups,0).count);
      for(i=0;i<128;++i) {
         for(j=0;j<32;++j) CHECK(xe_spv_groups_append(&groups,(uint32_t)i,records+j,allocate,release));
         view=xe_spv_groups_view(&groups,(uint32_t)i);
         CHECK(view.count==32);
         for(j=0;j<32;++j) CHECK(view.records[j]==records+j);
      }
   }
   CHECK(calls==allocations);
   groups.epoch=UINT32_MAX;
   xe_spv_groups_reset(&groups);
   CHECK(groups.epoch==1 && groups.count==128);
   CHECK(!xe_spv_groups_view(&groups,0).count);
   CHECK(xe_spv_groups_append(&groups,0,records+63,allocate,release));
   CHECK(xe_spv_groups_view(&groups,0).records[0]==records+63);
   CHECK(xe_spv_group_key(19,0)!=xe_spv_group_key(19,1));
   CHECK(!xe_spv_groups_append(&groups,0,NULL,allocate,release));
   xe_spv_groups_close(&groups,release);
   CHECK(allocations==releases && !groups.count && !groups.slots);
   puts("PASS: 409600 warm group appends, stable cross-group views, epoch/wrap reset, growth failures and cleanup");
   return 0;
}
