/* Observe selected payload independently from float comparison. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
#include "libretro/core_alu_spv.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"selection fixture line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
size_t xe_selection_spirv(const uint32_t*,size_t,uint32_t*,size_t,unsigned);
unsigned xe_alu_control_reference(const uint32_t*,const uint32_t*,unsigned,const uint32_t*,unsigned,unsigned,unsigned,uint32_t*,const uint32_t*);
void xe_alu_program_export(uint32_t *out,unsigned target,const void *value,uint32_t mask)
{
 unsigned i;if(target==62)return;CHECK(target==0);
 for(i=0;i<4;++i)if(mask&(1u<<i))memcpy(out+i,(const unsigned char*)value+i*4,4);
 out[6]|=mask;++out[5];
}
#define XE_TEST_XFB_PARAMETER0
#include "shader_xfb_fixture.h"
static uint32_t input[65536],output[65560],temporary[256],constants[24],context[40];
static const uint32_t edges[16]={0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,
 0x3f800000u,0xbf800000u,0x00800000u,0x80800000u,0x7f800000u,0xff800000u,
 0x7fc12345u,0xffc12345u,0x7f812345u,0x7f7fffffu};
/* Compare zero/subnormal magnitudes as zero, but retain the selected word. */
static uint32_t selected(uint32_t a,uint32_t b,unsigned minimum)
{
 uint32_t ma=a&0x7fffffffu,mb=b&0x7fffffffu;unsigned equal,less=0;
 if(ma>0x7f800000u || mb>0x7f800000u)return b;
 if(ma<0x00800000u)ma=0;
 if(mb<0x00800000u)mb=0;
 equal=ma==mb && (!ma || (a>>31)==(b>>31));
 if(!equal)less=(a>>31)!=(b>>31)?(a>>31):(a>>31)?ma>mb:ma<mb;
 return (minimum?less:!less)?a:b;
}
static int same(uint32_t a,uint32_t b)
{return a==b || ((a&0x7fffffffu)>0x7f800000u && (b&0x7fffffffu)>0x7f800000u);}
static void save(const char *path,const void *data,size_t bytes)
{
 RFILE *f=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);
 CHECK(f && filestream_write(f,data,bytes)==(int64_t)bytes && !filestream_close(f));
}
int main(int argc,char **argv)
{
 uint32_t code[9],actual[7],golden[4],a,b;unsigned which,i,pair,minimum,identity,scalar,mismatches=0,controls=1;size_t words;char path[512];
 CHECK((argc==2 || argc==3) && strlen(argv[1])<400);
 if(argc==3){CHECK(!strcmp(argv[2],"1") || !strcmp(argv[2],"3"));controls=(unsigned)atoi(argv[2]);}
 for(which=0;which<648;++which){
  memset(code,0,sizeof(code));memset(constants,0,sizeof(constants));
  scalar=which>=136;identity=which>=128 && !scalar;
  minimum=scalar?(which-136)/256:identity?(which-128)/4:which/64;
  for(i=0;i<4;++i){
   pair=scalar?(which-136)%256:identity?((which-128)%4)*4+i:(which%64)*4+i;
   a=edges[identity?pair:pair/16];b=identity?a:edges[pair%16];
   if(scalar){constants[0]=b;constants[3]=a;}
   else{constants[i]=a;constants[4+i]=b;}
   golden[i]=selected(a,b,minimum);
  }
  code[0]=1|(2u<<12);code[1]=0x2000;
  code[3]=(1u<<15)|(scalar?15u<<20:15u<<16)|((scalar?5u+minimum:50u)<<26);
  code[5]=scalar?0:((identity?0u:1u)<<8)|((2u+minimum)<<24);
  /* Constant-one position export: no nonfinite viewport contamination. */
  code[6]=62|(1u<<15)|(15u<<16)|(15u<<20)|(50u<<26);code[8]=2u<<24;
  CHECK(xe_alu_control_reference(code,temporary,64,constants,6,0,5,actual,context)==1);
  for(i=0;i<4;++i)if(!same(actual[i],golden[i]))++mismatches;
  words=xe_selection_spirv(code,9,input,65536,controls);CHECK(words>=5);
  sprintf(path,"%s/call%u.raw.spv",argv[1],which);save(path,input,words*4);
  words=shader_test_xfb(input,words,output,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.input",argv[1],which);save(path,constants,sizeof(constants));
  sprintf(path,"%s/call%u.loops",argv[1],which);save(path,context+8,128);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 printf("SELECTION_OBSERVATION cases=648 CPU_lane_disagreements=%u; compare=FTZ selected=raw\n",mismatches);
 printf("SELECTION_MODULE_CONTROLS signed_zero_inf_nan_preserve=1 FTZ=%u\n",(controls&2)!=0);
#ifdef XE_ALU_CONTROL_MINMAX_RAW_SELECTION
 CHECK(mismatches==0);
#endif
 return 0;
}
