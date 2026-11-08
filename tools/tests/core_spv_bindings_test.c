#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
static size_t allocations,frees;
static int fail;
static void *test_alloc(size_t size)
{
   void *p;
   if(fail)return NULL;
   p=malloc(size);if(p)++allocations;return p;
}
static void test_free(void *p) {if(p)++frees;free(p);}
#define XE_SPV_BINDINGS_ALLOC test_alloc
#define XE_SPV_BINDINGS_FREE test_free
#include "libretro/core_spv_bindings.h"
typedef char binding_record_size_is_eight[(sizeof(xe_spv_binding_record)==8)?1:-1];
#define CHECK(x) do {if(!(x)){fprintf(stderr,"bindings line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static unsigned char seen[16384];
int main(void)
{
   xe_spv_bindings table={0};
   xe_spv_binding_record *record,*borrowed;
   size_t i,capacity,old_allocations;
   uint32_t key=123,fetch,dimension,sign,mag,min,mip,aniso;
   CHECK(xe_spv_bindings_find(&table,0)==(size_t)-1);
   for(fetch=0;fetch<32;++fetch)for(dimension=0;dimension<4;++dimension)
   for(sign=0;sign<2;++sign) {
      CHECK(xe_spv_texture_binding_key(&key,fetch,dimension,sign));
      CHECK(key<256 && !seen[key]);seen[key]=1;
      CHECK((key&31)==fetch && ((key>>5)&3)==dimension && ((key>>7)&1)==sign);
   }
   memset(seen,0,sizeof(seen));
   for(fetch=0;fetch<32;++fetch)for(mag=0;mag<4;++mag)for(min=0;min<4;++min)
   for(mip=0;mip<4;++mip)for(aniso=0;aniso<8;++aniso) {
      CHECK(xe_spv_sampler_binding_key(&key,fetch,mag,min,mip,aniso));
      CHECK(key<sizeof(seen) && !seen[key]);seen[key]=1;
      CHECK((key&31)==fetch && ((key>>5)&3)==mag && ((key>>7)&3)==min &&
            ((key>>9)&3)==mip && ((key>>11)&7)==aniso);
   }
   key=123;
   CHECK(!xe_spv_texture_binding_key(NULL,0,0,0));
   CHECK(!xe_spv_texture_binding_key(&key,32,0,0) && key==123);
   CHECK(!xe_spv_texture_binding_key(&key,0,4,0) && key==123);
   CHECK(!xe_spv_texture_binding_key(&key,0,0,2) && key==123);
   CHECK(!xe_spv_sampler_binding_key(NULL,0,0,0,0,0));
   CHECK(!xe_spv_sampler_binding_key(&key,32,0,0,0,0) && key==123);
   CHECK(!xe_spv_sampler_binding_key(&key,0,4,0,0,0) && key==123);
   CHECK(!xe_spv_sampler_binding_key(&key,0,0,4,0,0) && key==123);
   CHECK(!xe_spv_sampler_binding_key(&key,0,0,0,4,0) && key==123);
   CHECK(!xe_spv_sampler_binding_key(&key,0,0,0,0,8) && key==123);
   for(i=0;i<XE_SPV_BINDINGS_INLINE;++i) {
      record=xe_spv_bindings_append(&table,(uint32_t)i);CHECK(record);
      CHECK(record==table.local+i && !record->variable);record->variable=(uint32_t)(100+i);
   }
   CHECK(!allocations && !table.heap);
   borrowed=xe_spv_bindings_data(&table);fail=1;
   CHECK(!xe_spv_bindings_append(&table,16));
   CHECK(table.count==16 && !table.heap && borrowed==xe_spv_bindings_data(&table));
   fail=0;
   for(i=16;i<1024;++i) {
      record=xe_spv_bindings_append(&table,(uint32_t)i);CHECK(record);
      record->variable=(uint32_t)(100+i);
   }
   for(i=0;i<1024;++i) {
      CHECK(xe_spv_bindings_find(&table,(uint32_t)i)==i);
      CHECK(xe_spv_bindings_view(&table)[i].variable==100+i);
   }
   CHECK(xe_spv_bindings_find(&table,UINT32_MAX)==(size_t)-1);
   borrowed=xe_spv_bindings_data(&table);capacity=table.capacity;fail=1;
   CHECK(!xe_spv_bindings_append(&table,1024));
   CHECK(table.count==1024 && table.capacity==capacity && borrowed==table.heap);
   CHECK(!xe_spv_bindings_reserve(&table,(size_t)-1));fail=0;
   old_allocations=allocations;table.count=0;
   for(i=0;i<1024;++i) {
      record=xe_spv_bindings_append(&table,(uint32_t)(1023-i));CHECK(record);
      CHECK(record==borrowed+i && !record->variable);
   }
   CHECK(allocations==old_allocations && table.capacity==capacity);
   table.count=(size_t)-1;CHECK(!xe_spv_bindings_append(&table,0));table.count=1024;
   xe_spv_bindings_close(&table);CHECK(allocations==frees);
   CHECK(!table.heap && !table.count && !table.capacity);
   xe_spv_bindings_close(&table);CHECK(allocations==frees);
   CHECK(xe_spv_bindings_append(&table,0)==table.local);
   xe_spv_bindings_close(&table);
   puts("PASS: exhaustive binding keys, inline/grown storage, exact lookup, OOM and warmed reuse");
   return 0;
}
