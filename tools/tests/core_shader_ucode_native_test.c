/* Guest-source mutation, host/big-endian snapshots and construction cost. C89. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <retro_endianness.h>
#ifndef XE_SHADER_COMPILE_BENCH
#define XE_SHADER_COMPILE_BENCH
#endif
#include "shader_compile_bench.h"
void *xe_ucode_open(const uint32_t*,size_t,unsigned);
void xe_ucode_close(void*);
const uint32_t *xe_ucode_view(void*,size_t*);
size_t xe_ucode_shader_size(void);
unsigned xe_ucode_required_alignment(void);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native ucode line %u\n",(unsigned)__LINE__);return 1;}}while(0)
static uint32_t input[65540],expected[65536];
int main(int argc,char **argv) {
 static const size_t counts[12]={0,1,3,4,5,16,64,257,4095,4096,12288,65536};
 unsigned swap,alignment,case_index;size_t i,count,got;void *owner;const uint32_t *view;
#ifdef XE_SHADER_COMPILE_BENCH
 unsigned repeat;
#endif
 (void)argc;(void)argv;
 for(swap=0;swap<2;++swap)for(alignment=0;alignment<4;++alignment)for(case_index=0;case_index<12;++case_index){
  count=counts[case_index];
  for(i=0;i<count;++i){uint32_t value=(uint32_t)i*UINT32_C(0x9e3779b9)+case_index;
   input[alignment+i]=value;expected[i]=swap?SWAP32(value):value;}
  owner=xe_ucode_open(count?input+alignment:NULL,count,swap);CHECK(owner);
  view=xe_ucode_view(owner,&got);CHECK(got==count);
  if(count>=1024 && xe_ucode_required_alignment())CHECK(((uintptr_t)view&31)==0);
  if(count)CHECK(view!=input+alignment);
  for(i=0;i<count;++i){CHECK(view[i]==expected[i]);input[alignment+i]^=UINT32_C(0xffffffff);}
  for(i=0;i<count;++i)CHECK(view[i]==expected[i]);
  CHECK(xe_ucode_view(owner,&got)==view && got==count);xe_ucode_close(owner);
 }
 printf("PASS:96 actual Shader snapshots, both endian modes/4 alignments, empty/tail/256KiB payloads, stable read-only views after source mutation; sizeof(Shader)=%lu\n",(unsigned long)xe_ucode_shader_size());
#ifdef XE_SHADER_COMPILE_BENCH
 for(swap=0;swap<2;++swap)for(case_index=0;case_index<12;++case_index){
  count=counts[case_index];for(i=0;i<count;++i)input[i]=(uint32_t)i+1;
  XE_COMPILE_BEGIN();
  for(repeat=0;repeat<2000;++repeat){
   owner=xe_ucode_open(count?input:NULL,count,swap);CHECK(owner);
   view=xe_ucode_view(owner,&got);CHECK(got==count);
   if(count)CHECK(view[count-1]==(swap?SWAP32((uint32_t)count):(uint32_t)count));
   xe_ucode_close(owner);
  }
  XE_COMPILE_END("shader.ucode.snapshot_batch2000",swap*12+case_index);
 }
#endif
 return 0;
}
