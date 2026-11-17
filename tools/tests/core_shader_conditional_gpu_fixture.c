/* Native CND payload audit; report CPU disagreements without changing goldens.
 * Immutable module/payload views use bounded reused C89 storage. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
#include "shader_conditional_fixture.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"conditional fixture line%u\n",(unsigned)__LINE__);exit(1);}}while(0)
size_t xe_rounding_spirv(const uint32_t*,size_t,uint32_t*,size_t);
unsigned xe_alu_control_reference(const uint32_t*,const uint32_t*,unsigned,const uint32_t*,unsigned,unsigned,unsigned,uint32_t*,const uint32_t*);
void xe_alu_program_export(uint32_t *out,unsigned target,const void *value,uint32_t mask) {
 unsigned lane;if(target==62)return;CHECK(target==0);
 for(lane=0;lane<4;++lane)if(mask&(1u<<lane))memcpy(out+lane,(const unsigned char*)value+lane*4,4);
 out[6]|=mask;++out[5];
}
#define XE_TEST_XFB_PARAMETER0
#include "shader_xfb_fixture.h"
static uint32_t module[65536],captured[65560],temporary[256],context[40];
static void save(const char *path,const void *data,size_t bytes) {
 RFILE *f=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);int64_t wrote;int closed;
 CHECK(f);wrote=filestream_write(f,data,bytes);closed=filestream_close(f);CHECK(wrote==(int64_t)bytes && !closed);
}
int main(int argc,char **argv) {
 uint32_t code[12],input[16],golden[4],actual[7];unsigned which,lane,differences=0;size_t words;char path[512];
 CHECK(argc==2 && strlen(argv[1])<400);
 for(which=0;which<84;++which){
  memset(code,0,sizeof(code));conditional_inputs(input,which);
  /* EXEC one CND to parameter0; EXEC_END a finite position from c3. */
  code[0]=2|(1u<<12);code[1]=0x1000u|((3u|(1u<<12))<<16);code[2]=0x20000000u;
  code[6]=(1u<<15)|(15u<<16)|(50u<<26);code[8]=2u|(1u<<8)|((12u+which%3)<<24);
  code[9]=62u|(1u<<15)|(15u<<16)|(50u<<26);code[11]=(3u<<16)|(3u<<8)|(2u<<24);
  CHECK(xe_alu_control_reference(code,temporary,64,input,4,0,3,actual,context)==1);
  for(lane=0;lane<4;++lane){
   golden[lane]=conditional_golden(which,lane);
   if(actual[lane]!=golden[lane] && !((actual[lane]&0x7fffffffu)>0x7f800000u && (golden[lane]&0x7fffffffu)>0x7f800000u)){
    if(differences<8)fprintf(stderr,"conditional CPU case%u lane%u %08x != raw selected %08x\n",which,lane,actual[lane],golden[lane]);
    ++differences;
   }
  }
  words=xe_rounding_spirv(code,12,module,65536);CHECK(words>=5);
  sprintf(path,"%s/call%u.raw.spv",argv[1],which);save(path,module,words*4);
  words=shader_test_xfb(module,words,captured,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,captured,words*4);
  sprintf(path,"%s/call%u.input",argv[1],which);save(path,input,sizeof(input));
  sprintf(path,"%s/call%u.loops",argv[1],which);save(path,context+8,128);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 printf("CONDITIONAL_AUDIT programs=84 CPU_lane_differences=%u; raw payload literals, last12 probe condition FTZ\n",differences);
 return differences?1:0;
}
