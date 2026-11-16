/* Same packed cases through the real native VS translator. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native saturation line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
size_t xe_forward_spirv_portable(const uint32_t*,size_t,unsigned,uint32_t*,size_t,unsigned*,uint32_t*);
size_t xe_saturation_spirv(const uint32_t*,size_t,uint32_t*,size_t);
unsigned xe_alu_control_reference(const uint32_t*,const uint32_t*,unsigned,const uint32_t*,unsigned,unsigned,unsigned,uint32_t*,const uint32_t*);
void xe_alu_program_export(uint32_t *out,unsigned target,const void *value,uint32_t mask)
{
 unsigned i;if(target==62)return;CHECK(target==0);
 for(i=0;i<4;++i)if(mask&(1u<<i))memcpy(out+i,(const unsigned char*)value+i*4,4);
 out[6]|=mask;++out[5];
}
static void alu(uint32_t *p,unsigned dest,unsigned vm,unsigned sm,unsigned sop,
 unsigned vop,unsigned a,unsigned b,unsigned c,unsigned temps)
{
 p[0]=dest|(vm<<16)|(sm<<20)|(sop<<26);p[1]=0;
 p[2]=(a<<16)|(b<<8)|c|(vop<<24)|((temps&1)?0x80000000u:0)|
  ((temps&2)?0x40000000u:0)|((temps&4)?0x20000000u:0);
}
static void control_cf(uint32_t *p,unsigned index,uint32_t a,uint32_t b)
{
 unsigned word=(index/2)*3;
 if(index&1){p[word+1]|=a<<16;p[word+2]=(a>>16)|(b<<16);}
 else{p[word]=a;p[word+1]=b;}
}
#include "shader_saturation_fixture.h"
#define XE_TEST_XFB_PARAMETER0
#include "shader_xfb_fixture.h"
static uint32_t input[65536],output[65560],temporary[256],constants[24],context[40];
static void save(const char *path,const void *data,size_t bytes)
{
 RFILE *f=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);
 CHECK(f && filestream_write(f,data,bytes)==(int64_t)bytes && !filestream_close(f));
}
int main(int argc,char **argv)
{
 uint32_t code[21],actual[7],golden[4],descriptor[16];unsigned which,i,sample;size_t words;char path[512];
 CHECK(argc==2 && strlen(argv[1])<400);
 for(which=0;which<160;++which){
  saturation_program(code,which);saturation_context(context,which);saturation_window(descriptor,which);
  /* Capture the ALU through parameter0, before viewport arithmetic. Append a
   * finite position export without changing either tested ALU's operands. */
  code[9]&=~63u;code[1]&=65535u;control_cf(code,1,3|(2u<<12),0x2000);
  /* Native shader bindings pack only used constants. Give both frontends a
   * dense c0=tested sample,c1=finite position namespace, preserving values. */
  sample=which<128?which/8:which<144?10:which-144;
  for(i=0;i<4;++i){constants[i]=saturation_constant(sample*4+i);constants[4+i]=0x3f800000u;}
  code[8]&=0xff000000u;
  alu(code+12,62|(1u<<15),15,0,50,2,1,1,0,0);
  descriptor[5]=6;descriptor[7]=5;
  CHECK(saturation_status(which)==1);
  CHECK(xe_alu_control_reference(code,temporary,64,constants,descriptor[5],descriptor[6],descriptor[7],actual,context)==1);
  for(i=0;i<4;++i){golden[i]=saturation_golden(which,i);
   if(actual[i]!=golden[i]){fprintf(stderr,"native saturation case%u lane%u: %08x != %08x\n",which,i,actual[i],golden[i]);return 1;}}
  words=xe_saturation_spirv(code,21,input,65536);CHECK(words>=5);
  sprintf(path,"%s/call%u.raw.spv",argv[1],which);save(path,input,words*4);
  words=shader_test_xfb(input,words,output,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.input",argv[1],which);save(path,constants,sizeof(constants));
  sprintf(path,"%s/call%u.loops",argv[1],which);save(path,context+8,128);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 puts("PASS: 160 native saturation fixtures and literal outputs");return 0;
}
