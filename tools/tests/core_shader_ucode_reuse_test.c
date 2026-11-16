/* Bounded ownership/reuse/failure accounting. MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static size_t allocations,frees,live,peak;
static unsigned fail_next,guard_error;
static void *test_allocate(size_t bytes)
{
 unsigned char *p;
 if(fail_next){fail_next=0;return NULL;}
 p=(unsigned char*)malloc(bytes+32);if(!p)return NULL;
 memcpy(p,&bytes,sizeof(bytes));memset(p+16+bytes,0x7c,16);
 memset(p+16,0xa5,bytes);++allocations;live+=bytes;if(live>peak)peak=live;
 return p+16;
}
static void test_free(void *allocation)
{
 size_t bytes,i;unsigned char *p;
 if(!allocation)return;
 p=(unsigned char*)allocation-16;memcpy(&bytes,p,sizeof(bytes));
 for(i=0;i<16;++i)if(p[16+bytes+i]!=0x7c)guard_error=1;
 live-=bytes;++frees;free(p);
}
#define XE_UCODE_REUSE_ALLOC test_allocate
#define XE_UCODE_REUSE_FREE test_free
#include "libretro/core_shader_ucode_reuse.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"reuse line %u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void)
{
 xe_shader_ucode_reuse a={0},b={0};uint32_t *borrow;size_t before,i,round,count;
 CHECK(!xe_shader_ucode_reuse_allocate(NULL,1));xe_shader_ucode_reuse_close(NULL);
 CHECK(xe_shader_ucode_reuse_allocate(&a,0) && !allocations && !a.words && !a.count);
 CHECK(!xe_shader_ucode_reuse_allocate(&a,(size_t)-1/4+1) && !allocations);
 CHECK(!xe_shader_ucode_reuse_allocate(&a,(size_t)-1/4) && !allocations);
 fail_next=1;CHECK(!xe_shader_ucode_reuse_allocate(&a,1024) && !a.words && !a.count);
 CHECK(xe_shader_ucode_reuse_allocate(&a,4095));borrow=a.words;
 CHECK(((uintptr_t)borrow&31)==0);
 for(i=0;i<a.count;++i)CHECK(borrow[i]==UINT32_C(0xa5a5a5a5));
 for(i=0;i<a.count;++i)borrow[i]=(uint32_t)i;
 before=allocations;
 CHECK(!xe_shader_ucode_reuse_allocate(&a,4096) && allocations==before && a.words==borrow);
 CHECK(xe_shader_ucode_reuse_allocate(&b,4095) && b.words!=a.words);
 for(i=0;i<a.count;++i)CHECK(borrow[i]==(uint32_t)i);
 xe_shader_ucode_reuse_close(&b);xe_shader_ucode_reuse_close(&a);
 CHECK(live==16384+OVERHEAD && allocations==frees+1);
 before=allocations;
 for(round=0;round<10000;++round){
  CHECK(xe_shader_ucode_reuse_allocate(&a,round&1?4095:4096));
  CHECK(a.words==borrow && allocations==before);
  a.words[0]=(uint32_t)round;a.words[a.count-1]=(uint32_t)~round;
  xe_shader_ucode_reuse_close(&a);CHECK(!a.words && !a.count);
 }
 CHECK(live==16384+OVERHEAD);before=frees;
 xe_shader_ucode_reuse_close(&a);CHECK(frees==before);
 /* Invalid calls do not consume or evict the existing cached block. */
 CHECK(!xe_shader_ucode_reuse_allocate(&a,(size_t)-1));
 CHECK(xe_shader_ucode_reuse_allocate(&a,4096) && allocations==before+1);
 xe_shader_ucode_reuse_close(&a);
 /* Mismatch evicts, OOM leaves an empty owner and no retained allocation. */
 fail_next=1;CHECK(!xe_shader_ucode_reuse_allocate(&a,12288) && !a.words && !a.count);
 CHECK(!live && allocations==frees);
 for(round=0;round<300;++round){
  count=round<4?1023+round:round*257;
  CHECK(xe_shader_ucode_reuse_allocate(&a,count));
  if(count>=1024)CHECK(((uintptr_t)a.words&31)==0);
  for(i=0;i<count;++i)a.words[i]=(uint32_t)(i^round);
  CHECK(xe_shader_ucode_reuse_allocate(&b,count));
  CHECK(a.words!=b.words);
  for(i=0;i<count;++i)CHECK(a.words[i]==(uint32_t)(i^round));
  xe_shader_ucode_reuse_close(&a);xe_shader_ucode_reuse_close(&b);
  CHECK(live<=MAX_BYTES+OVERHEAD && !guard_error);
 }
 xe_shader_ucode_reuse_clear();xe_shader_ucode_reuse_clear();
 CHECK(!live && allocations==frees && !guard_error && peak>MAX_BYTES);
 puts("PASS: bounded reuse, live-owner exclusion, size classes, overflow/OOM, redzones, eviction/drain, 10000 allocation-free reopens");
 return 0;
}
