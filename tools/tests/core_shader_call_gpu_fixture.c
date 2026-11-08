/* Actual guest VS -> SPIR-V + Xfb annotations; control flow is untouched. C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shader_call_fixture.h"
size_t xe_forward_spirv_portable(const uint32_t*,size_t,unsigned,uint32_t*,size_t,unsigned*,uint32_t*);
int xe_forward_interpret_constants(const uint32_t*,unsigned,const float*,float*,uint32_t);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"GPU fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static uint32_t input[65536],output[65560];
#include "shader_xfb_fixture.h"
static void save(const char *path,const void *data,size_t bytes) {
 FILE *f=fopen(path,"wb");CHECK(f && fwrite(data,1,bytes,f)==bytes && !fclose(f));
}
int main(int argc,char **argv) {
 uint32_t code[54],bools;float initial[8]={0},golden[4];
 size_t count,words;unsigned which,labels,i,mode,bool_constant,predicate;float wanted;char path[512];
 CHECK(argc==2 && strlen(argv[1])<400);
 for(which=0;which<16;++which) {
  mode=which%5;bool_constant=which>=5 && which<10;predicate=which>=10 && which<15;
  count=nested_scenario(code,0,mode);
  if(predicate) {
   memset(code,0,6*4);cf(code,0,0x1011,0x1000);cf(code,1,0x2004,0x9000);
   cf(code,2,0x100f,0x1000);cf(code,3,0x201c,0xb000);
   code[51]=0x6c000000;code[52]=0;code[53]=0xe2000004;count=54;
  }
  wanted=mode==1?(bool_constant?5.0f:3.0f):mode==2?(bool_constant?3.0f:5.0f):
     mode==3?(predicate?5.0f:3.0f):mode==4?(predicate?3.0f:5.0f):5.0f;
  if(which==15) {
   memset(code,0,sizeof(code));cf(code,0,0x2001,0x9000);cf(code,1,0x1010,0x1000);
   cf(code,2,0,0xa000);cf(code,3,0x1011,0x2000);
   code[51]=0xc80f803e;code[52]=0;code[53]=0xc2000000;count=54;wanted=2.0f;
  }
  /* ADD r0.x,r0.x,c0.y: explicit constant input, not injected temporary GPRs. */
  i=which==15?48:45;code[i]=0xc8010000;code[i+1]=0x00000100;code[i+2]=0xa0000000;
  CHECK(xe_forward_interpret_constants(code,0,initial,golden,bool_constant));
  CHECK(golden[0]==wanted);
  for(i=1;i<4;++i)CHECK(golden[i]==0);
  words=xe_forward_spirv_portable(code,count,0,input,65536,&labels,&bools);CHECK(words>=5);
  sprintf(path,"%s/call%u.raw.spv",argv[1],which);save(path,input,words*4);
  words=shader_test_xfb(input,words,output,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 puts("PASS: sixteen real guest vertex subroutine fixtures with explicit constants, independent goldens and annotation-only transform feedback");return 0;
}
