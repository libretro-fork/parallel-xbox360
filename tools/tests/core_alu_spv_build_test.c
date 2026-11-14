/* Raw arithmetic/program modules: allocation failure and reuse. MSVC C89. */
#include "libretro/core_alu_spv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations,frees,fail_next;
static void *allocate(void *p,size_t bytes) {
 ++allocations;if(fail_next){fail_next=0;return NULL;}return realloc(p,bytes);
}
static void release(void *p){if(p)++frees;free(p);}
#define XE_SPV_BINARY_REALLOC allocate
#define XE_SPV_BINARY_FREE release
/* Resolve through the tested source root, including when this frozen fixture
 * is compiled by a newer harness against another revision. */
#include "libretro/core_spv_binary.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ALU build line %u\n",(unsigned)__LINE__);return 1;}}while(0)
#ifdef XE_ALU_CONTROL_SHARED_DOT_LOOP
#define XE_TEST_CONTROL_LOOPS 3u
#elif defined(XE_ALU_CONTROL_BORROW_REGISTERS)
#define XE_TEST_CONTROL_LOOPS 2u
#else
#define XE_TEST_CONTROL_LOOPS 3u
#endif
#ifdef XE_ALU_CONTROL_DOT
#define XE_TEST_CONTROL_FUNCTIONS 5u
#elif defined(XE_ALU_CONTROL_MINMAX)
#define XE_TEST_CONTROL_FUNCTIONS 4u
#else
#define XE_TEST_CONTROL_FUNCTIONS 3u
#endif
int main(int argc,char **argv){
 int (*builders[])(xe_spv_binary*)={xe_alu_spv_build,xe_alu_program_spv_build,xe_alu_control_spv_build
#ifdef XE_ALU_RESUME_ABI
 ,xe_alu_control_resume_spv_build
#endif
 };
 xe_spv_binary module={0},reference={0};uint32_t *owner;
 size_t at,n,words;unsigned mode,builds,functions,loops,warm,before,constant64,array64,array_pointer,local_arrays,cold_requests;
 FILE *file;CHECK(argc>=3 && (unsigned)argc<=sizeof(builders)/sizeof(builders[0])+1);
 for(mode=0;mode<(unsigned)argc-1;++mode){
  CHECK(!builders[mode](NULL));fail_next=1;
  CHECK(!builders[mode](&module));CHECK(!module.words && !module.count && module.failed);
  before=allocations;CHECK(builders[mode](&module));
  cold_requests=allocations-before;
#ifdef XE_ALU_CONTROL_MINMAX
  CHECK(allocations==before+1);
  if(mode==2)printf("CONTROL_COLD_STORAGE allocations=%u capacity_words=%lu\n",allocations-before,(unsigned long)module.capacity);
#endif
  CHECK(builders[mode](&reference));
  owner=module.words;words=module.count;warm=allocations;
  CHECK(words>5 && words<(mode==3?16384:mode==2?12288:mode?8192:4096) && owner[0]==0x07230203 && owner[1]==0x10000);
  functions=0;loops=0;constant64=0;array64=0;array_pointer=0;local_arrays=0;
  for(at=5;at<words;at+=n){
   unsigned op=owner[at]&65535u;n=owner[at]>>16;CHECK(n && n<=words-at);
   CHECK(op!=11 && op!=22 && op!=129 && op!=133);
   if(op==21){CHECK(n==4);CHECK(owner[at+2]==32);}
   if(op==17){CHECK(n==2);CHECK(owner[at+1]==1);}
   if(op==54)++functions;
   if(op==246)++loops;
   if(op==43 && n==4 && owner[at+3]==64)constant64=owner[at+2];
   if(op==28 && n==4 && constant64 && owner[at+3]==constant64)array64=owner[at+1];
   if(op==32 && n==4 && array64 && owner[at+2]==7 && owner[at+3]==array64)array_pointer=owner[at+1];
   if(op==59 && array_pointer && owner[at+1]==array_pointer)++local_arrays;
  }
  CHECK(at==words && functions==(mode>=2?XE_TEST_CONTROL_FUNCTIONS:3u) && loops==(mode>=2?XE_TEST_CONTROL_LOOPS:mode?2u:0u));
  CHECK(local_arrays==(mode && mode!=3?1u:0u));
  if(mode==3)printf("RESUME_COLD_STORAGE allocations=%u capacity_words=%lu local_arrays=%u\n",cold_requests,(unsigned long)module.capacity,local_arrays);
  for(builds=0;builds<1000;++builds){
   CHECK(builders[mode](&module));CHECK(module.words==owner && module.count==words);
   CHECK(reference.count==words && !memcmp(module.words,reference.words,words*4));
   CHECK(allocations==warm);
  }
  CHECK(!xe_spv_binary_reserve(&module,(size_t)-1));CHECK(module.words==owner && module.failed);
  CHECK(builders[mode](&module) && module.words==owner && allocations==warm);
  file=fopen(argv[mode+1],"wb");CHECK(file && fwrite(owner,4,words,file)==words && !fclose(file));
  printf("PASS: %s %lu words, integer-only, 1000 same-owner exact builds/no allocations, failure/recovery\n",mode==3?"resume":mode==2?"control":mode?"program":"ALU",(unsigned long)words);
  before=frees;xe_spv_binary_close(&module);xe_spv_binary_close(&reference);
  CHECK(frees==before+2);xe_spv_binary_close(&module);CHECK(frees==before+2);
 }
 return 0;
}
