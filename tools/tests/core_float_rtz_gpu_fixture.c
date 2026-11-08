/* Real packed guest ALU, independent RTZ oracle and mixed SIMD lanes. C89. */
#include "libretro/core_float_rtz.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
size_t xe_rtz_native_spirv(const uint32_t*,size_t,unsigned,unsigned,unsigned,uint32_t*,size_t);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"RTZ GPU fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static uint32_t input[65536],output[65560];
#include "shader_xfb_fixture.h"
static void save(const char *path,const void *data,size_t bytes) {
 RFILE *f=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);
 CHECK(f && filestream_write(f,data,bytes)==(int64_t)bytes && !filestream_close(f));
}
static uint32_t oracle(unsigned op,uint32_t a,uint32_t b) {
 float f,rounded,x,y,native;uint32_t fb,native_bits;
 memcpy(&x,&a,4);memcpy(&y,&b,4);
 if(op==0){native=x+y;memcpy(&native_bits,&native,4);return xe_float_add_rtz_bits(a,b,native_bits);}
 if(op==1) {
  if((a&0x7fffffff)<0x00800000 || (b&0x7fffffff)<0x00800000)return 0;
  native=x*y;memcpy(&native_bits,&native,4);return xe_float_mul_rtz_bits(a,b,native_bits);
 }
 if((a&0x7fffffff)>=0x7f800000)return 0x7fc00000;
 if(!(a&0x7f800000))a&=0x80000000;
 memcpy(&f,&a,4);rounded=(float)floor((double)f);memcpy(&fb,&rounded,4);
 return xe_float_add_rtz_bits(a,fb^UINT32_C(0x80000000),0);
}
int main(int argc,char **argv) {
 static const uint32_t edges[10][8]={
  {0x3f800000,0xbf800000,0x00800000,0,0x33c00000,0xb3c00000,0x80800000,0},
  {0x3f800801,0xbf800801,0x00800000,0,0x3f800801,0x3f800801,0x3f400000,0},
  {0x7f7fffff,0xff7fffff,0x00800000,0,0x7f7fffff,0xff7fffff,0x007fffff,0},
  {0x80800000,0x00800000,0x80000000,0,0x00000001,0x80000001,0x80000000,0},
  {0x00800000,0x80800000,0x3f800000,0,0x3f000000,0x3f400000,0x3f7fffff,0},
  {0x3f800000,0x3f800001,0xbf800001,0,0xbf800000,0xbf800000,0x3f800000,0},
  {0x80800000,0x80000001,0xc0100000,0,0,0x3f800000,0xbf000000,0},
  {0x3e800000,0xbe800000,0x40000000,0,0x3f400000,0x3f400000,0xbf800000,0},
  {0x7f800000,0xff800000,0x7fc12345,0,0xff800000,0xff800000,0x3f800000,0},
  {0x00000001,0x80000001,0x00000000,0,0x7f800000,0xff800000,0x7fc12345,0}};
 uint32_t code[6],values[8],golden[4],seed=123456789;
 unsigned which,sample,op,c,hardware,index;uint32_t sum;size_t words;char path[512];
 CHECK(argc==2 && strlen(argv[1])<400);
 for(which=0;which<384;++which) {
  index=which%192;op=index%3+(which>=192?3:0);
  sample=(index/3)%32;hardware=index<96;
  if(sample<10)memcpy(values,edges[sample],sizeof(values));
  else for(c=0;c<8;++c){seed=seed*1664525u+1013904223u;values[c]=seed&UINT32_C(0xff7fffff);}
  if(op<3) {
   values[3]=op==2?0x3fc00000:0x3f800000;values[7]=op==1?0x3f800000:0;
   for(c=0;c<4;++c)golden[c]=oracle(op,values[c],values[c+4]);
  } else {
   if(!sample) {
    /* Ordered RTZ loses each 0.75 ULP; reassociation incorrectly retains them. */
    values[0]=0x3f800000;values[1]=values[2]=values[3]=0x33c00000;
    for(c=4;c<8;++c)values[c]=0x3f800000;
   }
   sum=oracle(1,values[0],values[4]);
   for(c=1;c<7-op;++c)sum=oracle(0,sum,oracle(1,values[c],values[c+4]));
   if(op==5)sum=oracle(0,sum,values[0]);
   for(c=0;c<4;++c)golden[c]=sum;
  }
  for(c=0;c<3;++c)if(!(golden[c]&0x7fffffff))golden[c]=0;
  memset(code,0,sizeof(code));code[0]=0x1001;code[1]=0x2000;
  code[3]=62|(1u<<15)|(15u<<16)|(50u<<26);
  code[5]=(1u<<8)|((op<3?(op==2?8:op):op+12)<<24);
  words=xe_rtz_native_spirv(code,6,0,hardware,0,input,65536);CHECK(words>=5);
  words=shader_test_xfb(input,words,output,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.input",argv[1],which);save(path,values,sizeof(values));
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 puts("PASS: 384 actual guest ALU/dot fixtures, hardware/software RTZ with explicit mixed-lane inputs and integer oracle");return 0;
}
