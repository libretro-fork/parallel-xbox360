/* Actual Shader owner: allocation/factory failures and stable identity. C89. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"variant owner line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
void *xe_variant_open(void*);void xe_variant_close(void*);void *xe_variant_find(void*,uint64_t);
int xe_variant_create(void*,uint64_t,int*,void**);void xe_variant_remove(void*,uint64_t);
uint64_t xe_variant_serial(const void*);void xe_variant_sizes(size_t*,size_t*);unsigned xe_variant_intrusive(void);
typedef union allocation_header {size_t bytes;unsigned char alignment[16];} allocation_header;
static size_t live,calls,requested;static int budget=-1;
void *xe_variant_allocate(size_t bytes) {
 allocation_header *p;if(!budget || bytes>(size_t)-1-sizeof(*p))return NULL;
 if(budget>0)--budget;
 p=(allocation_header*)malloc(sizeof(*p)+bytes);if(!p)return NULL;
 p->bytes=bytes;live+=bytes;++calls;requested+=bytes;return p+1;
}
void xe_variant_free(void *p) {
 allocation_header *header;if(!p)return;header=(allocation_header*)p-1;
 CHECK(live>=header->bytes);live-=header->bytes;free(header);
}
typedef struct fixture {
 unsigned mode,depth,attempts,retired;
 uint64_t next,retired_serial,retired_key;
 void *inner;
} fixture;
uint64_t xe_variant_factory(void *opaque,uint64_t key,void *owner) {
 fixture *f=(fixture*)opaque;void *p;int fresh=0;++f->attempts;
 if(f->mode==1)return UINT64_MAX;
 if(f->mode==2)return 0;
 if(f->mode==3 && !f->depth){
  f->depth=1;CHECK(xe_variant_create(owner,key,&fresh,&p)==1 && fresh);f->inner=p;f->depth=0;
 }
 return ++f->next;
}
void xe_variant_retired(void *opaque,uint64_t key,uint64_t serial) {
 fixture *f=(fixture*)opaque;++f->retired;f->retired_serial=serial;f->retired_key=key;
}
static uint64_t key_for(unsigned i) {
 return i==511?UINT64_MAX:UINT64_C(0x9e3779b97f4a7c15)*i;
}
#ifndef XE_SHADER_VARIANT_PROFILE
int main(void) {
 fixture f={0};void *owner,*p,*saved[512];uint64_t serial[512];unsigned i,retired;
 int fresh,status;size_t before,allocations,bytes,owner_size,value_size,initial=live;
 CHECK(xe_variant_intrusive());owner=xe_variant_open(&f);CHECK(owner);
 before=live;allocations=calls;CHECK(!xe_variant_find(owner,0));CHECK(calls==allocations);
 for(i=0;i<512;++i){
  allocations=calls;bytes=requested;fresh=0;
  CHECK(xe_variant_create(owner,key_for(i),&fresh,&saved[i])==1 && fresh);
  CHECK(calls==allocations+1);serial[i]=xe_variant_serial(saved[i]);
  CHECK(requested>bytes && xe_variant_find(owner,key_for(i))==saved[i]);
 }
 for(i=0;i<512;++i){
  allocations=calls;fresh=99;CHECK(xe_variant_create(owner,key_for(i),&fresh,&p)==1 && !fresh);
  CHECK(p==saved[i] && xe_variant_serial(p)==serial[i] && calls==allocations);
 }
 for(i=0;i<512;i+=2)xe_variant_remove(owner,key_for(i));
 for(i=0;i<512;++i){
  p=xe_variant_find(owner,key_for(i));CHECK(p==(i&1?saved[i]:NULL));
  if(p)CHECK(xe_variant_serial(p)==serial[i]);
 }
 for(i=0;i<512;++i)xe_variant_remove(owner,key_for(i));
 CHECK(f.retired==512 && live==before);
 for(i=1;i<=2;++i){
  f.mode=i;allocations=calls;fresh=99;p=owner;
  status=xe_variant_create(owner,123,&fresh,&p);
  CHECK(status==(i==1?-1:0) && !p && !xe_variant_find(owner,123));
  CHECK(calls==allocations && live==before && fresh==(i==1?99:0));
 }
 f.mode=0;budget=0;fresh=99;CHECK(xe_variant_create(owner,123,&fresh,&p)==-1 && !p && fresh==99);
 CHECK(live==before && !xe_variant_find(owner,123));
 budget=1;fresh=0;CHECK(xe_variant_create(owner,123,&fresh,&p)==1 && fresh && !budget);
 budget=-1;xe_variant_remove(owner,123);CHECK(live==before);
 f.mode=3;retired=f.retired;fresh=99;
 CHECK(xe_variant_create(owner,123,&fresh,&p)==1 && !fresh && p==f.inner);
 CHECK(xe_variant_find(owner,123)==f.inner && f.retired==retired+1);
 CHECK(f.retired_serial>xe_variant_serial(f.inner));
 xe_variant_remove(owner,123);CHECK(f.retired==retired+2 && live==before);
 f.mode=0;for(i=0;i<512;++i)CHECK(xe_variant_create(owner,key_for(i),NULL,&p)==1);
 retired=f.retired;xe_variant_sizes(&owner_size,&value_size);xe_variant_close(owner);
 CHECK(live==initial && f.retired==retired+512);
 printf("PASS: production Shader owner; one object allocation/miss, zero bookkeeping allocations, factory failure/reentrancy, stable identities and balanced destruction; owner=%lu value=%lu\n",(unsigned long)owner_size,(unsigned long)value_size);
 return 0;
}

#else
#include "shader_variant_profile_fixture.h"
#endif
