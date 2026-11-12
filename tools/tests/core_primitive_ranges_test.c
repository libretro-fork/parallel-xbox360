/* Cold ownership, exact guest bound and warm reuse. MSVC C89. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"primitive ranges line %d\n",__LINE__);exit(1); } } while(0)
static void *range_alloc(size_t);
static void range_free(void*);
#define XE_PRIMITIVE_RANGES_ALLOC range_alloc
#define XE_PRIMITIVE_RANGES_FREE range_free
#include "libretro/core_primitive_ranges.h"

static unsigned allocations,frees,fail_allocation;
static unsigned char *allocation;
static size_t allocation_size;
static uint16_t source16[65535];
static uint32_t source32[65535];
static void *range_alloc(size_t bytes)
{
   ++allocations;
   CHECK(bytes==262140 && !allocation);
   if(fail_allocation)return NULL;
   allocation=(unsigned char*)malloc(bytes+32);CHECK(allocation);
   allocation_size=bytes;memset(allocation,0xa5,bytes+32);
   return allocation+16;
}
static void guards(void)
{
   size_t i;
   CHECK(allocation);
   for(i=0;i<16;++i)CHECK(allocation[i]==0xa5 && allocation[16+allocation_size+i]==0xa5);
}
static void range_free(void *data)
{
   CHECK(data==allocation+16);guards();++frees;
   free(allocation);allocation=NULL;allocation_size=0;
}
static uint32_t line_count(uint32_t count)
{return count>=2?count+1:0;}
static uint32_t overflow_count(uint32_t count)
{(void)count;return UINT32_MAX;}
static void sources(void)
{
   unsigned i;
   for(i=0;i<65535;++i) {
      source16[i]=i%3==2?UINT16_MAX:(uint16_t)(i%31+1);
      source32[i]=i%3==2?UINT32_C(0x89abcdef):UINT32_C(0xfe000000)+(i%31+1);
   }
}
static void maximum(xe_primitive_ranges *ranges)
{
   unsigned width,i;
   uint32_t total;
   const void *source;
   xe_primitive_range *identity=ranges->data;
   for(width=2;width<=4;width+=2) {
      sources();xe_primitive_ranges_clear(ranges);
      source=width==2?(const void*)source16:(const void*)source32;
      total=xe_primitive_ranges_append(ranges,line_count,source,65535,width,
            width==2?UINT16_MAX:UINT32_C(0xabcdef),UINT32_C(0xffffff));
      CHECK(total==65535 && ranges->count==21845 && ranges->data==identity);
      /* Changing borrowed source later must not change retained segmentation. */
      memset(source16,0xff,sizeof(source16));memset(source32,0,sizeof(source32));
      for(i=0;i<21845;++i) {
         CHECK(ranges->data[i].guest_offset==i*3);
         CHECK(ranges->data[i].guest_index_count==2);
         CHECK(ranges->data[i].host_index_count==3);
      }
      guards();
   }
}
static void rejection(xe_primitive_ranges *ranges)
{
   uint32_t capacity=ranges->capacity;
   xe_primitive_ranges invalid={NULL,0,1};
   xe_primitive_ranges_clear(ranges);
   CHECK(xe_primitive_ranges_push(ranges,17,2,3));ranges->capacity=2;
   source16[0]=1;source16[1]=2;source16[2]=UINT16_MAX;source16[3]=3;source16[4]=4;
   CHECK(xe_primitive_ranges_append(ranges,line_count,source16,5,2,UINT16_MAX,UINT16_MAX)==UINT32_MAX);
   CHECK(ranges->count==1 && ranges->data[0].guest_offset==17 &&
         ranges->data[0].guest_index_count==2 && ranges->data[0].host_index_count==3);
   ranges->capacity=capacity;
   CHECK(xe_primitive_ranges_append(ranges,overflow_count,source16,2,2,UINT16_MAX,0)==UINT32_MAX);
   CHECK(ranges->count==1);
   CHECK(xe_primitive_ranges_append(ranges,line_count,(const void*)(uintptr_t)1,65536,4,0,0)==UINT32_MAX);
   CHECK(xe_primitive_ranges_append(ranges,line_count,source16,1,3,0,0)==UINT32_MAX);
   CHECK(xe_primitive_ranges_append(ranges,line_count,source16,1,2,65536,0)==UINT32_MAX);
   CHECK(xe_primitive_ranges_append(ranges,line_count,(const unsigned char*)source32+1,1,4,0,0)==UINT32_MAX);
   CHECK(xe_primitive_ranges_append(ranges,NULL,source16,1,2,0,0)==UINT32_MAX);
   CHECK(xe_primitive_ranges_append(&invalid,line_count,NULL,0,2,0,0)==UINT32_MAX);
   CHECK(xe_primitive_ranges_append(ranges,line_count,NULL,0,2,0,0)==0 && ranges->count==1);
   CHECK(!xe_primitive_ranges_push(ranges,65535,1,3));
   CHECK(ranges->count==1);guards();
}
static void warm(xe_primitive_ranges *ranges)
{
   unsigned i,before=allocations;
   xe_primitive_range *identity=ranges->data;
   sources();
   for(i=0;i<2000;++i) {
      xe_primitive_ranges_clear(ranges);
      CHECK(ranges->data==identity && ranges->capacity==21845 && !ranges->count);
      CHECK(xe_primitive_ranges_append(ranges,line_count,source16,510,2,UINT16_MAX,0)==510);
      CHECK(ranges->count==170 && ranges->data[169].guest_offset==507);
   }
   CHECK(allocations==before && !frees);guards();
}
int main(void)
{
   xe_primitive_ranges ranges={NULL,0,0};
   CHECK(!xe_primitive_ranges_init(NULL));
   fail_allocation=1;CHECK(!xe_primitive_ranges_init(&ranges));
   CHECK(!ranges.data && !ranges.count && !ranges.capacity && allocations==1);
   fail_allocation=0;CHECK(xe_primitive_ranges_init(&ranges));
   CHECK(!xe_primitive_ranges_init(&ranges) && allocations==2);
   maximum(&ranges);rejection(&ranges);warm(&ranges);
   xe_primitive_ranges_close(&ranges);xe_primitive_ranges_close(&ranges);
   CHECK(!ranges.data && !ranges.count && !ranges.capacity && frees==1);
   CHECK(xe_primitive_ranges_init(&ranges));xe_primitive_ranges_close(&ranges);
   CHECK(frees==2 && allocations==3);
   puts("PASS: exact 21845-row guest bound, both index widths, retained snapshots, rollback, fault/close and 2000 allocation-free warm reuses");
   return 0;
}
