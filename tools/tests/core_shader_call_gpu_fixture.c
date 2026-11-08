/* Actual guest VS -> SPIR-V + Xfb annotations; control flow is untouched. C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shader_call_fixture.h"
size_t xe_forward_spirv_portable(const uint32_t*,size_t,unsigned,uint32_t*,size_t,unsigned*,uint32_t*);
int xe_forward_interpret_constants(const uint32_t*,unsigned,const float*,float*,uint32_t);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"GPU fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static uint32_t input[65536],output[65560],pointers[65536];
static size_t xfb(const uint32_t *words,size_t count) {
 size_t at,n,out=5;uint32_t vertex_type=0,variable=0,entry=0,id;
 unsigned op,mode=0,decor=0;
 CHECK(count>=5 && words[0]==0x07230203 && words[3]<65536);
 memset(pointers,0,sizeof(pointers));
 for(at=5;at<count;at+=n) {
  n=words[at]>>16;op=words[at]&65535;CHECK(n && n<=count-at);
  if(op==72 && n==5 && words[at+2]==0 && words[at+3]==11 && words[at+4]==0)vertex_type=words[at+1];
  if(op==32 && n==4 && words[at+2]==3){id=words[at+1];CHECK(id<65536);pointers[id]=words[at+3];}
  if(op==15){CHECK(!entry && n>=4 && words[at+1]==0);entry=words[at+2];}
 }
 CHECK(vertex_type && entry);
 for(at=5;at<count;at+=n) {
  n=words[at]>>16;op=words[at]&65535;
  if(op==59 && n>=4 && words[at+3]==3 && words[at+1]<65536 && pointers[words[at+1]]==vertex_type) {
   CHECK(!variable);variable=words[at+2];
  }
 }
 CHECK(variable);memcpy(output,words,20);
 output[out++]=(2u<<16)|17;output[out++]=53; /* TransformFeedback capability. */
 for(at=5;at<count;at+=n) {
  n=words[at]>>16;op=words[at]&65535;
  if(!mode && op!=17 && op!=10 && op!=11 && op!=14 && op!=15 && op!=16) {
   output[out++]=(3u<<16)|16;output[out++]=entry;output[out++]=11;mode=1; /* Xfb. */
  }
  if(!decor && op>=19 && op<=39) {
   output[out++]=(4u<<16)|71;output[out++]=variable;output[out++]=36;output[out++]=0;
   output[out++]=(4u<<16)|71;output[out++]=variable;output[out++]=37;output[out++]=16;
   output[out++]=(5u<<16)|72;output[out++]=vertex_type;output[out++]=0;output[out++]=35;output[out++]=0;
   decor=1;
  }
  CHECK(out+n<=65560);memcpy(output+out,words+at,n*4);out+=n;
 }
 CHECK(mode && decor);return out;
}
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
  words=xfb(input,words);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 puts("PASS: sixteen real guest vertex subroutine fixtures with explicit constants, independent goldens and annotation-only transform feedback");return 0;
}
