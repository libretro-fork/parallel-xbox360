/* Actual renderer dot zero-sign probe, literal expected ordered replicated -0. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <streams/file_stream.h>
size_t xe_rtz_native_spirv(const uint32_t*,size_t,unsigned,unsigned,unsigned,uint32_t*,size_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"renderer dot zero probe line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static uint32_t input[65536],output[65560];
#include "shader_xfb_fixture.h"
static void save(const char *path,const void *p,size_t bytes) {
 RFILE *file=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);
 CHECK(file && filestream_write(file,p,bytes)==(int64_t)bytes && !filestream_close(file));
}
int main(int argc,char **argv) {
 uint32_t code[6],values[12],golden[4]={0x80000000u,0x80000000u,0x80000000u,0x80000000u};
 unsigned which,lane,op,hardware;size_t words;char path[768];
 CHECK(argc==2 && strlen(argv[1])<650);
 for(which=0;which<6;++which){
  op=which%3;hardware=which/3;memset(code,0,sizeof(code));
  code[0]=0x1001;code[1]=0x2000;code[3]=62|(1u<<15)|(15u<<16)|(50u<<26);
  code[5]=(1u<<8)|2|((15+op)<<24);
  for(lane=0;lane<4;++lane){values[lane]=0x00800000u;values[4+lane]=0xbf000000u;values[8+lane]=0x80000000u;}
  words=xe_rtz_native_spirv(code,6,0,hardware,0,input,65536);CHECK(words>=5);
  words=shader_test_xfb(input,words,output,65560);
  sprintf(path,"%s/call%u.spv",argv[1],which);save(path,output,words*4);
  sprintf(path,"%s/call%u.input",argv[1],which);save(path,values,op==2?48:32);
  sprintf(path,"%s/call%u.expected",argv[1],which);save(path,golden,sizeof(golden));
 }
 puts("PASS:6 actual renderer dot fixtures, native/portable RTZ, all-negative nonzero underflow and -0 third input; literal expected all four components=-0");return 0;
}
