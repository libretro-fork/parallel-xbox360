/* Observable loop-index witness, not an out-of-bounds addressing test. C89.
 * Only iteration15 consumes aL: the unbounded sum selects valid r10, while
 * saturating the iterator selects r13. All other relative reads are skipped. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
size_t xe_forward_spirv_portable(const uint32_t*,size_t,unsigned,uint32_t*,size_t,unsigned*,uint32_t*);
unsigned xe_loop_accuracy_native(const uint32_t*,const uint32_t*,const float*,float*);
static uint32_t input[65536],output[65560],code[128],loops[32];
static float constants[24];
#define CHECK(x) do{if(!(x)){fprintf(stderr,"loop advance witness line %u\n",(unsigned)__LINE__);exit(2);}}while(0)
#include "shader_xfb_fixture.h"
static void cf(unsigned index,uint32_t a,uint32_t b)
{
 unsigned word=(index/2)*3;
 if(index&1){code[word+1]|=a<<16;code[word+2]=(a>>16)|(b<<16);}
 else{code[word]=a;code[word+1]=b;}
}
static uint32_t pack(unsigned count,unsigned start,int step)
{return count|(start<<8)|(((uint32_t)step&255)<<16);}
static void instruction(unsigned slot,unsigned dest,unsigned vm,unsigned scalar_op,
 unsigned vector_op,unsigned a,unsigned b,unsigned c,unsigned temps)
{
 code[slot*3]=dest|(vm<<16)|(scalar_op<<26);code[slot*3+1]=0;
 code[slot*3+2]=(a<<16)|(b<<8)|c|(vector_op<<24)|
  ((temps&1)?UINT32_C(0x80000000):0)|((temps&2)?UINT32_C(0x40000000):0)|((temps&4)?UINT32_C(0x20000000):0);
}
static void save(const char *path,const void *data,size_t bytes)
{
 RFILE *f=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);
 CHECK(f && filestream_write(f,data,bytes)==(int64_t)bytes && !filestream_close(f));
}
int main(int argc,char **argv)
{
 unsigned i,j,emitted,labels;uint32_t bools;size_t words;
 float actual[4]={0},golden[4]={42,42,42,42};char path[512];
 CHECK(argc==2 && strlen(argv[1])<400);
 for(i=0;i<6;++i)for(j=0;j<4;++j)constants[i*4+j]=i==0?1.0f:i==1?-15.0f:i==3?42.0f:i==4?13.0f:0.0f;
 loops[0]=pack(3,255,127);loops[1]=pack(5,99,-127);
 cf(0,24|(3u<<12),0x1000);cf(1,7,0x7000);cf(2,6|(1u<<13)|(1u<<16),0x7000);
 cf(3,27|(3u<<12),0x1000);cf(4,30|(1u<<12),0x5000|(1u<<10));
 cf(5,3|(1u<<16),0x8000);cf(6,2,0x8000);cf(7,24,0x2000);
 instruction(24,0,15,50,2,2,2,0,0);
 instruction(25,10,15,50,2,3,3,0,0);instruction(26,13,15,50,2,4,4,0,0);
 instruction(27,0,15,50,0,0,0,0,1);instruction(28,1,15,50,0,0,1,0,1);
 instruction(29,0,0,27,0,0,0,1,4);
 instruction(30,62|(1u<<15),15,50,2,9|64,9|64,0,3);
 emitted=xe_loop_accuracy_native(code,loops,constants,actual);
 printf("LOOP_ADVANCE_NATIVE exports=%u value=%.9g expected=42; final consumed index10, other relative reads inactive\n",emitted,actual[0]);
 words=xe_forward_spirv_portable(code,93,0,input,65536,&labels,&bools);CHECK(words>=5);
 sprintf(path,"%s/call0.raw.spv",argv[1]);save(path,input,words*4);
 words=shader_test_xfb(input,words,output,65560);
 sprintf(path,"%s/call0.spv",argv[1]);save(path,output,words*4);
 sprintf(path,"%s/call0.expected",argv[1]);save(path,golden,sizeof(golden));
 sprintf(path,"%s/call0.input",argv[1]);save(path,constants,sizeof(constants));
 sprintf(path,"%s/call0.loops",argv[1]);save(path,loops,sizeof(loops));
 sprintf(path,"%s/call0.ucode",argv[1]);save(path,code,93*4);
 sprintf(path,"%s/call0.native",argv[1]);save(path,actual,sizeof(actual));
 CHECK(emitted==1);
 for(j=0;j<4;++j)if(actual[j]!=golden[j])return 1;
 puts("PASS: loop advance native witness42; no saturated iterator");return 0;
}
