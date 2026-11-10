/* Analysis metadata and binary identity with/without diagnostic text. C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 for(pixel=0;pixel<2;++pixel){
  for(mode=0;mode<5;++mode){n=nested_scenario(code,pixel,mode);n=xe_analysis_compare(code,n,pixel);CHECK(n);bytes+=n;++cases;}
  for(mode=0;mode<4;++mode)for(i=0;i<4;++i){n=stress(pixel,lengths[mode],i);n=xe_analysis_compare(code,n,pixel);CHECK(n);bytes+=n;++cases;}
 }
 n=stress(0,1,0);CHECK(xe_analysis_dump_switch(code,n,argv[1]));
 printf("PASS: %u VS/PS analysis metadata and emitted binary identities; %lu diagnostic bytes absent in quiet mode; dump opt-in preserved\n",cases,(unsigned long)bytes);return 0;
}
