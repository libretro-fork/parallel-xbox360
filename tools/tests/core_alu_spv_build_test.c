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
#include "../../libretro/core_spv_binary.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ALU build line %u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(int argc,char **argv){
 int (*builders[2])(xe_spv_binary*)={xe_alu_spv_build,xe_alu_program_spv_build};
 xe_spv_binary module={0},reference={0};uint32_t *owner;
 size_t at,n,words;unsigned mode,builds,functions,loops,warm,before;
 FILE *file;CHECK(argc==3);
 for(mode=0;mode<2;++mode){
  CHECK(!builders[mode](NULL));fail_next=1;
  CHECK(!builders[mode](&module));CHECK(!module.words && !module.count && module.failed);
  CHECK(builders[mode](&module));CHECK(builders[mode](&reference));
  owner=module.words;words=module.count;warm=allocations;
  CHECK(words>5 && words<(mode?8192:4096) && owner[0]==0x07230203 && owner[1]==0x10000);
  functions=0;loops=0;
  for(at=5;at<words;at+=n){
   unsigned op=owner[at]&65535u;n=owner[at]>>16;CHECK(n && n<=words-at);
   CHECK(op!=11 && op!=22 && op!=129 && op!=133);
   if(op==21){CHECK(n==4);CHECK(owner[at+2]==32);}
   if(op==17){CHECK(n==2);CHECK(owner[at+1]==1);}
   if(op==54)++functions;
   if(op==246)++loops;
  }
  CHECK(at==words && functions==3 && loops==(mode?2u:0u));
  for(builds=0;builds<1000;++builds){
   CHECK(builders[mode](&module));CHECK(module.words==owner && module.count==words);
   CHECK(reference.count==words && !memcmp(module.words,reference.words,words*4));
   CHECK(allocations==warm);
  }
  CHECK(!xe_spv_binary_reserve(&module,(size_t)-1));CHECK(module.words==owner && module.failed);
  CHECK(builders[mode](&module) && module.words==owner && allocations==warm);
  file=fopen(argv[mode+1],"wb");CHECK(file && fwrite(owner,4,words,file)==words && !fclose(file));
  printf("PASS: %s %lu words, integer-only, 1000 same-owner exact builds/no allocations, failure/recovery\n",mode?"program":"ALU",(unsigned long)words);
  before=frees;xe_spv_binary_close(&module);xe_spv_binary_close(&reference);
  CHECK(frees==before+2);xe_spv_binary_close(&module);CHECK(frees==before+2);
 }
 return 0;
}
