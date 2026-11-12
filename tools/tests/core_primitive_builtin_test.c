/* Independent emitted-index oracle; bounds and rejection are all C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_primitive_builtin.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"builtin line %d\n",__LINE__);exit(1); } } while(0)
#define CAPACITY (3*1024*1024)
#define STORAGE_BYTES (CAPACITY+32)
/* Untyped allocation matches a mapped upload; a declared uint32_t array would
 * make the uint16_t output stores violate the C effective-type rules. */
static uint32_t *storage;
static size_t range_size(unsigned kind,unsigned strips)
{
   if(!kind)return strips?((size_t)strips*5-1)*4:0;
   return kind==2?(size_t)(65535/4)*12:(size_t)(65535-2)*6;
}
static void verify_pattern(const xe_primitive_builtin_layout *l,size_t cursor,int gaps)
{
   size_t offset,i,j,bytes;
   uint32_t actual,want;
   uint16_t small;
   unsigned kind;
   unsigned char *data=(unsigned char*)(storage+4);
   for(kind=0;kind<4;++kind) {
      offset=l->offsets[kind];if(offset==SIZE_MAX)continue;
      bytes=range_size(kind,l->two_triangle_count);
      for(i=0;i<bytes/(kind?2:4);++i) {
         if(kind==0)want=i%5==4?UINT32_MAX:(uint32_t)((i/5)*4+i%5);
         else if(kind==1)want=i%3==0?(uint32_t)(i/3+1):i%3==1?(uint32_t)(i/3+2):0;
         else if(kind==2) {
            static const unsigned lane[6]={0,1,2,0,2,3};
            want=(uint32_t)(i/6*4+lane[i%6]);
         } else {
            j=i/3;
            /* Alternating first two vertices; keep independent literal pairs. */
            if(i%3==0)want=(uint32_t)(j+(j&1));
            else if(i%3==1)want=(uint32_t)(j+((j&1)?0:1));
            else want=(uint32_t)(j+2);
         }
         if(kind) { memcpy(&small,data+offset+i*2,2);actual=small; }
         else memcpy(&actual,data+offset+i*4,4);
         CHECK(actual==want);
      }
      if(gaps)for(i=0;i<16;++i)CHECK(data[offset+bytes+i]==0x6d);
   }
   for(i=0;i<4;++i)CHECK(storage[i]==UINT32_C(0x6d6d6d6d));
   for(i=cursor;i<CAPACITY+16;++i)CHECK(data[i]==0x6d);
}
static void pattern(unsigned mask,unsigned strips)
{
   xe_primitive_builtin_layout l;
   size_t cursor=0;
   unsigned kind;
   unsigned char *data=(unsigned char*)(storage+4);
   memset(storage,0x6d,STORAGE_BYTES);l.two_triangle_count=strips;
   for(kind=0;kind<4;++kind) {
      l.offsets[kind]=SIZE_MAX;
      if(mask&(1u<<kind)) {
         l.offsets[kind]=cursor;
         cursor+=range_size(kind,strips)+16;
      }
   }
   CHECK(cursor<=CAPACITY);
   CHECK(xe_primitive_builtin_fill(data,cursor,&l));
   verify_pattern(&l,cursor,1);
}
static void rejection(void)
{
   xe_primitive_builtin_layout l={{0,SIZE_MAX,SIZE_MAX,SIZE_MAX},1};
   xe_primitive_builtin_layout bad;
   unsigned char *data=(unsigned char*)(storage+4);
   unsigned i,kind;
   memset(storage,0x6d,STORAGE_BYTES);
   CHECK(!xe_primitive_builtin_fill(NULL,16,&l));
   CHECK(!xe_primitive_builtin_fill(data,16,NULL));
   CHECK(!xe_primitive_builtin_fill(data+1,16,&l));
   CHECK(!xe_primitive_builtin_fill(data,15,&l));
   bad=l;bad.offsets[0]=1;CHECK(!xe_primitive_builtin_fill(data,32,&bad));
   bad=l;bad.two_triangle_count=65536;CHECK(!xe_primitive_builtin_fill(data,CAPACITY,&bad));
   for(kind=0;kind<4;++kind) {
      bad=l;bad.offsets[kind]=CAPACITY;CHECK(!xe_primitive_builtin_fill(data,CAPACITY,&bad));
      bad=l;bad.offsets[kind]=SIZE_MAX-1;CHECK(!xe_primitive_builtin_fill(data,CAPACITY,&bad));
   }
   bad=l;bad.offsets[1]=0;CHECK(!xe_primitive_builtin_fill(data,CAPACITY,&bad));
   for(i=0;i<STORAGE_BYTES;++i)CHECK(((unsigned char*)storage)[i]==0x6d);
}
#ifdef XE_PRIMITIVE_NATIVE
#include "primitive_counts_protocol.h"
static uint16_t indices16[512];
static uint32_t indices32[512],rows[512*3],golden[512*3];
static unsigned host_count(unsigned topology,unsigned count)
{
   if(topology==1)return count>=2?count+1:0;
   if(topology==2)return (count/4)*6;
   return count>=3?(count-2)*3:0;
}
static void initialization(void)
{
   xe_primitive_builtin_layout actual,expected;
   size_t size,expected_size,i;
   unsigned flags,kind,calls;
   int enabled[4],ok,fail;
   for(flags=0;flags<64;++flags) {
      expected.two_triangle_count=!(flags&8)?65535:!(flags&16)?65535/3:0;
      enabled[0]=expected.two_triangle_count!=0;
      enabled[1]=!(flags&1);enabled[2]=!(flags&4);enabled[3]=(flags&32)!=0;
      expected_size=0;
      for(kind=0;kind<4;++kind) {
         expected.offsets[kind]=enabled[kind]?expected_size:SIZE_MAX;
         if(enabled[kind])expected_size+=range_size(kind,expected.two_triangle_count);
      }
      for(fail=0;fail<2;++fail) {
         memset(storage,0x6d,STORAGE_BYTES);size=SIZE_MAX;calls=UINT32_MAX;
         ok=xe_primitive_initialize(flags,storage+4,CAPACITY,&actual,&size,&calls,fail);
         CHECK(ok==(!fail || !expected_size));
         CHECK(calls==(expected_size?1u:0u) && size==expected_size);
         if(!fail) {
            CHECK(actual.two_triangle_count==expected.two_triangle_count);
            for(kind=0;kind<4;++kind)CHECK(actual.offsets[kind]==expected.offsets[kind]);
            verify_pattern(&actual,size,0);
         } else for(i=0;i<STORAGE_BYTES;++i)CHECK(((unsigned char*)storage)[i]==0x6d);
      }
   }
   puts("PASS: 64 actual primitive initialization policies, exact layouts/direct fill and 64 failure replays");
}
static void ranges(void)
{
   unsigned topology,width,test,count,i,begin,n,host,used,expected,total;
   uint32_t seed=UINT32_C(0x769843d1),mask,reset,value;
   const void *source;
   for(topology=0;topology<4;++topology)for(width=2;width<=4;width+=2)
      for(test=0;test<256;++test) {
         count=test<16?test:(test*37)%513;
         mask=width==2?UINT32_C(0xffff):test&1?UINT32_C(0x00ffffff):UINT32_MAX;
         reset=test&2?mask:0;
         for(i=0;i<count;++i) {
            seed=seed*UINT32_C(1664525)+UINT32_C(1013904223);
            value=(test%4==0 || (seed&7)==0)?reset:seed&mask;
            indices16[i]=(uint16_t)value;
            indices32[i]=value|((test&1)?seed&UINT32_C(0xff000000):0);
         }
         memset(rows,0x6d,sizeof(rows));memset(golden,0x6d,sizeof(golden));
         i=0;total=0;expected=0;
         while(i<=count) {
            begin=i;
            while(i<count && (width==2?(uint32_t)indices16[i]:(indices32[i]&mask))!=reset)++i;
            n=i-begin;host=host_count(topology,n);
            if(host) {
               golden[expected*3]=begin;golden[expected*3+1]=n;
               golden[expected*3+2]=host;++expected;total+=host;
            }
            if(i==count)break;
            ++i;
         }
         source=width==2?(const void*)indices16:(const void*)indices32;
         used=UINT32_MAX;
         CHECK(xe_primitive_count_ranges(topology,width,source,count,reset,mask,rows,512,&used)==total);
         CHECK(used==expected && !memcmp(rows,golden,sizeof(rows)));
      }
   puts("PASS: 2048 actual 16/32-bit primitive range/count cases and output guards");
}
static void persistent(void)
{
   void *context=xe_primitive_counts_open();
   unsigned i,used=123;
   CHECK(context);
   CHECK(xe_primitive_counts_run(NULL,0,2,NULL,0,0,0,NULL,0,&used)==UINT32_MAX && used==123);
   for(i=0;i<512;++i)indices16[i]=i%3==2?UINT16_MAX:(uint16_t)(i+1);
   for(i=0;i<128;++i) {
      memset(rows,0x6d,sizeof(rows));used=123;
      CHECK(xe_primitive_counts_run(context,1,2,indices16,510,UINT16_MAX,UINT16_MAX,rows,512,&used)==510 && used==170);
      CHECK(rows[0]==0 && rows[1]==2 && rows[2]==3 && rows[507]==507 && rows[508]==2 && rows[509]==3);
      CHECK(rows[510]==UINT32_C(0x6d6d6d6d));
      CHECK(xe_primitive_counts_run(context,1,2,indices16,510,UINT16_MAX,UINT16_MAX,NULL,0,&used)==510 && used==170);
   }
   used=123;
   CHECK(xe_primitive_counts_run(context,1,2,indices16,65536,0,0,NULL,0,&used)==UINT32_MAX && used==123);
   xe_primitive_counts_close(context);xe_primitive_counts_close(NULL);
   puts("PASS: persistent actual range owner, 256 warm calls, no-copy measurement mode and invalid context/bounds");
}
#endif
int main(void)
{
   static const unsigned counts[]={0,1,2,65535/3,65535};
   unsigned mask,i;
   storage=(uint32_t*)malloc(STORAGE_BYTES);CHECK(storage);
   for(mask=0;mask<16;++mask)for(i=0;i<sizeof(counts)/sizeof(counts[0]);++i)pattern(mask,counts[i]);
   rejection();puts("PASS: 80 primitive upload layouts, every emitted index, gaps/guards and preflight rejection");
#ifdef XE_PRIMITIVE_NATIVE
   ranges();initialization();persistent();
#endif
   free(storage);
   return 0;
}
