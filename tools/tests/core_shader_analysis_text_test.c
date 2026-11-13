/* Analysis metadata and binary identity with/without diagnostic text. C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XE_ANALYSIS_TEXT_ALLOCATIONS
static size_t text_allocations,text_bytes;
void *xe_analysis_text_allocate(size_t bytes){
 void *p=malloc(bytes);if(p){++text_allocations;text_bytes+=bytes;}return p;
}
#endif
#include "shader_call_fixture.h"
size_t xe_analysis_compare(const uint32_t*,size_t,unsigned);
int xe_analysis_dump_switch(const uint32_t*,size_t,const char*);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"analysis text line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static uint32_t code[3072];
static size_t stress(unsigned pixel,unsigned additions,unsigned opcode) {
 unsigned count=additions+1,chunks=(count+5)/6,payload=(chunks+1)/2,i,n;
 memset(code,0,sizeof(code));
 for(i=0;i<chunks;++i){n=count-i*6;if(n>6)n=6;cf(code,i,(payload+i*6)|(n<<12),i+1==chunks?0x2000:0x1000);}
 for(i=0;i<additions;++i){code[(payload+i)*3]=0xc80f0000;code[(payload+i)*3+2]=UINT32_C(0xc0000100)|(opcode<<24);}
 code[(payload+additions)*3]=(pixel?0:62)|(1u<<15)|(15u<<16)|(50u<<26);
 code[(payload+additions)*3+2]=0xc2000000;
 return (payload+count)*3;
}
int main(int argc,char**argv){unsigned pixel,mode,i,cases=0;size_t n,bytes=0;static const unsigned lengths[]={1,32,96,256};
 CHECK(argc==2);
#ifdef XE_ANALYSIS_TEXT_ALLOCATIONS
 /* Exclude process-initialization constructors from the analysis window. */
 printf("TEXT_STARTUP calls=%lu bytes=%lu\n",(unsigned long)text_allocations,(unsigned long)text_bytes);
 text_allocations=text_bytes=0;
#endif
 for(pixel=0;pixel<2;++pixel){
  for(mode=0;mode<5;++mode){n=nested_scenario(code,pixel,mode);n=xe_analysis_compare(code,n,pixel);CHECK(n);bytes+=n;++cases;}
  for(mode=0;mode<4;++mode)for(i=0;i<4;++i){n=stress(pixel,lengths[mode],i);n=xe_analysis_compare(code,n,pixel);CHECK(n);bytes+=n;++cases;}
 }
#ifdef XE_ANALYSIS_TEXT_ALLOCATIONS
 /* Only the explicit text sink and untouched-sink guard construct buffers;
  * quiet no-sink analysis must not construct a third 16KiB buffer. */
 printf("TEXT_CONSTRUCTORS calls=%lu bytes=%lu cases=%u\n",(unsigned long)text_allocations,(unsigned long)text_bytes,cases);
 CHECK(text_allocations==cases*2 && text_bytes==cases*2*16384);
 text_allocations=text_bytes=0;
#endif
 n=stress(0,1,0);CHECK(xe_analysis_dump_switch(code,n,argv[1]));
#ifdef XE_ANALYSIS_TEXT_ALLOCATIONS
 CHECK(text_allocations==2 && text_bytes==32768);
 puts("PASS: no quiet diagnostic-buffer allocation; opt-in allocates once per owner, repeated analysis allocates none (constructor requests only)");
#endif
 printf("PASS: %u VS/PS analysis metadata and emitted binary identities; %lu diagnostic bytes absent in quiet mode; dump opt-in preserved\n",cases,(unsigned long)bytes);return 0;
}
