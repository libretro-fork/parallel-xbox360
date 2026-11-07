/* C89 pointer identity/order oracle and bounded hash-collision regression. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_pointer_unique.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"unique line %d\n",__LINE__);exit(1);}} while(0)
struct item { unsigned char bytes[16]; };
XE_POINTER_UNIQUE_DEFINE(unique_items, struct item)
static struct item pool[16384];
static struct item *input[64], *wanted[64];
static struct { uintptr_t before;struct item *items[64];uintptr_t after; } work;
static unsigned rng=123456789;
static unsigned random_value(void) {rng=rng*1664525u+1013904223u;return rng;}
static void check(unsigned n)
{
   unsigned i,j,count=0,got;
   work.before=123;work.after=456;
   memset(work.items,0,sizeof(work.items));memcpy(work.items,input,n*sizeof(*input));
   for(i=0;i<n;++i) {
      for(j=0;j<count && wanted[j]!=input[i];++j) {}
      if(j==count) wanted[count++]=input[i];
   }
   got=unique_items(work.items,n);CHECK(got==count);
   for(i=0;i<count;++i) CHECK(work.items[i]==wanted[i]);
   CHECK(work.before==123 && work.after==456);
}
#ifdef XE_TEXTURE_UNIQUE_NATIVE
extern void* xe_unique_native_new(void);
extern void xe_unique_native_free(void*);
extern void xe_unique_native_dirty(void*,unsigned,unsigned);
extern unsigned xe_unique_native_state(void*,unsigned);
extern int xe_unique_native_span_unchanged(void*);
extern unsigned xe_unique_native_batch(void*,const unsigned*,unsigned,int,int,unsigned*,unsigned*);
static void native(void)
{
   void *owner=xe_unique_native_new();
   unsigned indices[64],order[64],masks[64],i,count;
   CHECK(owner);
   for(i=0;i<64;++i) indices[i]=0;
   count=xe_unique_native_batch(owner,indices,64,1,0,order,masks);
   printf("native batch: 64 aliases -> %u conversion calls\n",count);
   CHECK(count==1);
   CHECK(order[0]==0 && masks[0]==3 && !xe_unique_native_state(owner,0));
   CHECK(xe_unique_native_batch(owner,indices,64,1,0,order,masks)==0);
   xe_unique_native_dirty(owner,0,1);
   CHECK(xe_unique_native_batch(owner,indices,64,1,0,order,masks)==1);
   CHECK(masks[0]==1 && !xe_unique_native_state(owner,0));
   xe_unique_native_dirty(owner,0,2);
   CHECK(xe_unique_native_batch(owner,indices,64,0,0,order,masks)==1);
   CHECK(masks[0]==2 && xe_unique_native_state(owner,0)==2);
   CHECK(xe_unique_native_batch(owner,indices,64,1,0,order,masks)==1);
   CHECK(masks[0]==2 && !xe_unique_native_state(owner,0));
   /* Distinct signed owners of the same guest bytes both require conversion. */
   xe_unique_native_dirty(owner,0,3);
   for(i=0;i<64;++i) indices[i]=i&1;
   CHECK(xe_unique_native_batch(owner,indices,64,1,0,order,masks)==2);
   CHECK(order[0]==0 && order[1]==1 && !xe_unique_native_state(owner,0) && !xe_unique_native_state(owner,1));
   for(i=0;i<64;++i) indices[i]=(i/2)&1;
   CHECK(xe_unique_native_batch(owner,indices,64,1,0,order,masks)==0);
   CHECK(xe_unique_native_span_unchanged(owner));
   /* A guest invalidation during loading must keep the owner dirty/retryable. */
   for(i=0;i<64;++i) indices[i]=0;
   xe_unique_native_dirty(owner,0,3);
   CHECK(xe_unique_native_batch(owner,indices,64,1,1,order,masks)==1);
   CHECK(xe_unique_native_state(owner,0)==3);
   CHECK(xe_unique_native_batch(owner,indices,64,1,0,order,masks)==1);
   CHECK(!xe_unique_native_state(owner,0));
   for(i=0;i<64;++i) {indices[i]=i;xe_unique_native_dirty(owner,i,3);}
   count=xe_unique_native_batch(owner,indices,64,1,0,order,masks);CHECK(count==64);
   for(i=0;i<64;++i) CHECK(order[i]==i && !xe_unique_native_state(owner,i));
   indices[0]=8;indices[1]=7;indices[2]=8;indices[3]=6;indices[4]=7;
   for(i=0;i<5;++i) xe_unique_native_dirty(owner,indices[i],3);
   CHECK(xe_unique_native_batch(owner,indices,5,1,0,order,masks)==3);
   CHECK(order[0]==8 && order[1]==7 && order[2]==6);
   CHECK(xe_unique_native_batch(owner,indices,0,1,0,order,masks)==0);
   xe_unique_native_free(owner);
   puts("PASS: actual dirty batch dispatches, signed owners, watches and failed/invalidation retries");
}
#endif
int main(void)
{
   unsigned n,code,total,i,j,found,bucket;
   CHECK(unique_items(NULL,0)==0 && unique_items(NULL,65)==65);
   for(n=0;n<=8;++n) {
      total=1u<<(2*n);
      for(code=0;code<total;++code) {
         for(i=0;i<n;++i) input[i]=((code>>(2*i))&3)?&pool[(code>>(2*i))&3]:NULL;
         check(n);
      }
   }
   for(n=0;n<=64;++n) {
      for(i=0;i<n;++i) input[i]=&pool[i];
      check(n);
      for(i=0;i<n;++i) input[i]=&pool[0];
      check(n);
      for(i=0;i<n;++i) input[i]=&pool[i&1];
      check(n);
   }
   for(j=0;j<10000;++j) {
      n=random_value()%65;
      for(i=0;i<n;++i) input[i]=&pool[random_value()%128];
      check(n);
   }
   /* All64 distinct pointers share one initial bucket: bounded linear probing. */
   bucket=xe_pointer_unique_bucket((uintptr_t)&pool[0]);found=0;
   for(i=0;i<16384 && found<64;++i)
      if(xe_pointer_unique_bucket((uintptr_t)&pool[i])==bucket) input[found++]=&pool[i];
   CHECK(found==64);check(64);
   for(i=0;i<64;++i) input[i]=input[63-i/2];
   check(64);
   puts("PASS: stable typed pointer identity, exhaustive short sequences and 64-way collisions");
#ifdef XE_TEXTURE_UNIQUE_NATIVE
   native();
#endif
   return 0;
}
