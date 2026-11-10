/* Packed guest loop regressions: independent literal goldens, real CPU + VS.
 * Finite MAX operands avoid arithmetic-rounding dependencies. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"loop GPU fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
size_t xe_forward_spirv_portable(const uint32_t*,size_t,unsigned,uint32_t*,size_t,unsigned*,uint32_t*);
unsigned xe_loop_accuracy_native(const uint32_t*,const uint32_t*,const float*,float*);
static uint32_t input[65536],output[65560],code[128],loops[32];
static float constants[24];
#include "shader_xfb_fixture.h"
static void cf(unsigned index,uint32_t a,uint32_t b)
{
 unsigned word=(index/2)*3;
 if(index&1){code[word+1]|=a<<16;code[word+2]=(a>>16)|(b<<16);}
 else {code[word]=a;code[word+1]=b;}
}
static uint32_t pack(unsigned count,unsigned start,int step)
{return count|(start<<8)|(((uint32_t)step&255)<<16);}
static void alu(unsigned slot,unsigned dest,unsigned source,int relative)
{
 code[slot*3]=(dest&63)|(15u<<16)|(50u<<26)|(relative?1u<<15:0);
 code[slot*3+1]=0;
 code[slot*3+2]=((source|(relative?64:0))<<16)|
    ((source|(relative?64:0))<<8)|(2u<<24)|(relative?3u<<30:0);
}
static void save(const char *path,const void *data,size_t bytes)
{
 RFILE *f=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);
 CHECK(f && filestream_write(f,data,bytes)==(int64_t)bytes && !filestream_close(f));
}
int main(int argc,char **argv)
{
 static const unsigned indices[7][6]={
  {1,2,3,0,0,0},{6,4,2,0,0,0},{60,61,62,63,0,0},
  {3,4,5,6,7,0},{5,0,0,0,0,0},{0,1,2,0,0,0},{5,6,0,0,0,0}};
 static const unsigned counts[7]={3,4,4,5,1,3,2};
 static const unsigned last[7]={2,3,3,2,0,2,0};
 static const unsigned exports[7]={3,4,4,6,1,3,3};
 unsigned which,i,j,labels,emitted;uint32_t bools;size_t words;
 float actual[4],golden[4];char path[512];
 CHECK(argc==2 && strlen(argv[1])<400);
 for(which=0;which<7;++which){
  memset(code,0,sizeof(code));memset(loops,0,sizeof(loops));memset(constants,0,sizeof(constants));
  for(i=0;i<counts[which];++i){
   for(j=0;j<4;++j)constants[i*4+j]=(float)(10+i*8+j);
   alu(24+i,indices[which][i],i,0);
  }
  cf(0,24|(counts[which]<<12),0x1000);alu(30,62,0,1);
  if(which<3 || which==5){
   loops[0]=which==0?pack(3,1,1):which==1?pack(4,6,-2):
      which==2?pack(4,60,1):pack(3,99,1);
   cf(1,4|(which==5?1u<<13:0),0x7000);cf(2,30|(1u<<12),0x1000);
   cf(3,2,0x8000);cf(4,24,0x2000);
  }else if(which==3){
   loops[0]=pack(2,5,2);loops[1]=pack(3,99,-1);
   cf(1,6,0x7000);cf(2,5|(1u<<13)|(1u<<16),0x7000);
   cf(3,30|(1u<<12),0x1000);cf(4,3|(1u<<16),0x8000);
   cf(5,2,0x8000);cf(6,24,0x2000);
  }else if(which==4){
   for(i=0;i<4;++i){loops[i]=pack(1,i?99:5,1);
    cf(1+i,(10-i)|(i?1u<<13:0)|(i<<16),0x7000);}
   cf(5,30|(1u<<12),0x1000);
   for(i=0;i<4;++i)cf(6+i,(5-i)|((3-i)<<16),0x8000);
   cf(10,24,0x2000);
  }else{
   loops[0]=pack(1,5,1);loops[1]=pack(2,99,1);loops[2]=pack(1,99,-1);
   cf(1,9,0x7000);cf(2,5|(1u<<13)|(1u<<16),0x7000);
   cf(3,30|(1u<<12),0x1000);cf(4,3|(1u<<16),0x8000);
   cf(5,8|(1u<<13)|(2u<<16),0x7000);cf(6,30|(1u<<12),0x1000);
   cf(7,6|(2u<<16),0x8000);cf(8,2,0x8000);cf(9,24,0x2000);
  }
  memcpy(golden,constants+last[which]*4,sizeof(golden));
  emitted=xe_loop_accuracy_native(code,loops,constants,actual);
  if(emitted!=exports[which]){
   fprintf(stderr,"loop case %u exports: %u != %u\n",which,emitted,exports[which]);exit(1);
  }
  for(j=0;j<4;++j)if(actual[j]!=golden[j]){
   fprintf(stderr,"loop case %u lane %u: %.9g != %.9g\n",which,j,actual[j],golden[j]);exit(1);
  }
  words=xe_forward_spirv_portable(code,93,0,input,65536,&labels,&bools);CHECK(words>=5);
  sprintf(path,"%s/call%u.raw.spv",argv[1],which);save(path,input,words*4);
  words=shader_test_xfb(input,words,output,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
  sprintf(path,"%s/call%u.input",argv[1],which);save(path,constants,sizeof(constants));
  sprintf(path,"%s/call%u.loops",argv[1],which);save(path,loops,sizeof(loops));
 }
 puts("PASS: seven literal loop goldens: signed stride, repeat inheritance, four levels and slot reuse");return 0;
}
