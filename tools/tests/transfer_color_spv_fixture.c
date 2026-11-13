/* Exercise actual C89 builder and all its allocator requests. No retail data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static size_t allocations,fail_at;
void *color_test_malloc(size_t bytes){++allocations;return allocations==fail_at?NULL:malloc(bytes);}
void *color_test_calloc(size_t count,size_t bytes){++allocations;return allocations==fail_at?NULL:calloc(count,bytes);}
void *color_test_realloc(void *p,size_t bytes){++allocations;return allocations==fail_at?NULL:realloc(p,bytes);}
#define malloc color_test_malloc
#define calloc color_test_calloc
#define realloc color_test_realloc
#define XE_TRANSFER_COLOR_DYNAMIC_TEST
#include "../../libretro/core_spv_binary.c"
#include "../../libretro/core_transfer_color_spv.c"
#undef malloc
#undef calloc
#undef realloc
#define CHECK(x) do{if(!(x)){fprintf(stderr,"color SPV line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
int main(int argc,char **argv)
{
 static const uint32_t scales[3][2]={{1,1},{3,5},{4,4}};
 xe_spv_binary code={0},failed={0};xe_transfer_color_options options={0};
 xe_transfer_address_packet_config config={1,1,0};
 xe_transfer_format_spv counter;size_t impossible=(size_t)-1,old_capacity,offset,minimum=(size_t)-1,maximum=0;
 uint32_t next=1,args[2]={1,1};unsigned g,n,source,dest,ms,mode,target,cases=0;
 char name[4096];FILE *file;
 CHECK(argc==2 && strlen(argv[1])<3800);
 for(g=0;g<3;++g)for(n=0;n<2;++n)for(source=0;source<2;++source)for(dest=0;dest<2;++dest)
 for(ms=0;ms<2;++ms)for(mode=0;mode<3;++mode)for(target=0;target<4;++target){
  config.scale_x=scales[g][0];config.scale_y=scales[g][1];config.native2=n;
  options.source_uint=source;options.dest_uint=dest;options.source_multisampled=ms;options.sample_mode=mode;options.target=target;
  old_capacity=code.capacity;allocations=0;
  CHECK(xe_transfer_color_spv_build(&code,&options,&config));
  CHECK(allocations==(code.count>old_capacity?1u:0u) && code.capacity>=code.count);
  CHECK(code.count>5 && code.words[0]==UINT32_C(0x07230203) && code.words[3]>CDYNAMIC);
  for(offset=5;offset<code.count;offset+=code.words[offset]>>16)
   CHECK((code.words[offset]>>16)!=0 && (code.words[offset]>>16)<=code.count-offset);
  CHECK(offset==code.count);
  if(code.count<minimum)minimum=code.count;
  if(code.count>maximum)maximum=code.count;
  sprintf(name,"%s/color-%u-%u-%u-%u-%u-%u-%u.spv",argv[1],g,n,source,dest,ms,mode,target);
  file=fopen(name,"wb");CHECK(file);
  CHECK(fwrite(code.words,sizeof(*code.words),code.count,file)==code.count);CHECK(!fclose(file));++cases;
 }
 CHECK(cases==576);
 options.source_uint=2;allocations=0;old_capacity=code.count;
 CHECK(!xe_transfer_color_spv_build(&code,&options,&config) && !allocations && code.count==old_capacity);
 options.source_uint=0;allocations=0;fail_at=1;
 CHECK(!xe_transfer_color_spv_build(&failed,&options,&config) && allocations==1 && !failed.words && !failed.count && failed.failed);
 fail_at=0;xe_spv_binary_reset(&failed);CHECK(xe_transfer_color_spv_build(&failed,&options,&config));
 memset(&counter,0,sizeof(counter));counter.next=&next;counter.word_count=&impossible;
 CHECK(!xe_transfer_format_spv_instruction(&counter,SpvOpIAdd,2,args,2) && counter.failed && next==1);
 xe_spv_binary_close(&failed);xe_spv_binary_close(&code);
 printf("PASS: %u raw color image modules, one final allocation or reused backing per build, %lu..%lu words, allocation-failure retry and plan-overflow guards; GPU image validation pending\n",
  cases,(unsigned long)minimum,(unsigned long)maximum);
 return 0;
}
