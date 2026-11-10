/* Uninitialized final storage, overflow/failure and owner lifecycle. C89. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned allocations,frees,fail_next,guard_failure;
static void *snapshot_allocate(size_t bytes) {
 unsigned char *raw;uint64_t size=(uint64_t)bytes,guard=UINT64_C(0x1734deab83297564);
 ++allocations;if(fail_next){fail_next=0;return NULL;}
 if(bytes>(size_t)-1-24)return NULL;
 raw=(unsigned char*)malloc(bytes+24);if(!raw)return NULL;
 memset(raw,0xa5,bytes+24);memcpy(raw,&size,8);memcpy(raw+8,&guard,8);memcpy(raw+16+bytes,&guard,8);
 return raw+16;
}
static void snapshot_free(void *pointer) {
 unsigned char *raw;uint64_t bytes,head,tail;
 if(!pointer)return;
 raw=(unsigned char*)pointer-16;memcpy(&bytes,raw,8);memcpy(&head,raw+8,8);memcpy(&tail,raw+16+(size_t)bytes,8);
 if(head!=UINT64_C(0x1734deab83297564) || head!=tail)guard_failure=1;
 ++frees;free(raw);
}
#define XE_SHADER_UCODE_ALLOC snapshot_allocate
#define XE_SHADER_UCODE_FREE snapshot_free
#include "libretro/core_shader_ucode.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ucode owner line %u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void) {
 xe_shader_ucode owner={0};uint32_t *borrow;unsigned before,round;size_t i,count;
 CHECK(!xe_shader_ucode_allocate(NULL,1));xe_shader_ucode_close(NULL);
 CHECK(xe_shader_ucode_allocate(&owner,0) && !allocations && !owner.words && !owner.count);
 CHECK(!xe_shader_ucode_allocate(&owner,(size_t)-1/4+1) && !allocations);
 CHECK(!xe_shader_ucode_allocate(&owner,(size_t)-1/4) && !allocations);
 fail_next=1;CHECK(!xe_shader_ucode_allocate(&owner,31) && !owner.words && !owner.count);
 for(round=0;round<300;++round){
  count=round<3?1023u+round:round*17u+1;before=allocations;
  CHECK(xe_shader_ucode_allocate(&owner,count) && allocations==before+1 && owner.count==count);
  if(count>=1024)CHECK(((uintptr_t)owner.words&31)==0);
  borrow=owner.words;for(i=0;i<count;++i)CHECK(borrow[i]==UINT32_C(0xa5a5a5a5));
  for(i=0;i<count;++i)borrow[i]=(uint32_t)(i^round);
  CHECK(!xe_shader_ucode_allocate(&owner,count) && allocations==before+1 && owner.words==borrow && owner.count==count);
  for(i=0;i<count;++i)CHECK(borrow[i]==(uint32_t)(i^round));
  xe_shader_ucode_close(&owner);CHECK(!owner.words && !owner.count && !guard_failure);
  before=frees;xe_shader_ucode_close(&owner);CHECK(frees==before);
 }
 CHECK(allocations==frees+1);
 puts("PASS: exact uninitialized payload, source-independent owner, no refill/allocation on rejected reopen, overflow/failure/redzones/idempotent close");
 return 0;
}
