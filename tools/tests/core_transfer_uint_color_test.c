/* Scalar oracle for bit-preserving generic integer-view transfers. BSD; C89. */
#include "libretro/core_transfer_uint_color.h"
#include <stdio.h>
#include <string.h>
typedef struct machine {uint32_t values[128];unsigned count,attempts,fail_at;} machine;
static uintptr_t emit(void *context,enum xe_transfer_sample_op op,
 uintptr_t a,uintptr_t b,uintptr_t unused) {
 machine *m=(machine*)context;uint32_t v,x,y;
 (void)unused;++m->attempts;if(m->attempts==m->fail_at || m->count>=127)return 0;
 if(op==XE_TRANSFER_SAMPLE_CONST)v=(uint32_t)a;
 else {
  if(!a || !b || a>m->count || b>m->count)return 0;
  x=m->values[a];y=m->values[b];
  switch(op) {
   case XE_TRANSFER_SAMPLE_AND:v=x&y;break;
   case XE_TRANSFER_SAMPLE_OR:v=x|y;break;
   case XE_TRANSFER_SAMPLE_XOR:v=x^y;break;
   case XE_TRANSFER_SAMPLE_SHL:if(y!=16)return 0;v=x<<16;break;
   case XE_TRANSFER_SAMPLE_SHR:if(y!=16)return 0;v=x>>16;break;
   default:return 0;
  }
 }
 m->values[++m->count]=v;return m->count;
}
static uintptr_t literal(machine *m,uint32_t v){return emit(m,XE_TRANSFER_SAMPLE_CONST,v,0,0);}
static int check(unsigned format,const uint32_t input[4],unsigned fail_at,unsigned phase) {
 machine m={0};xe_transfer_sample_emitter e;xe_transfer_uint_color_config c;
 xe_transfer_uint_color_parameters p;uintptr_t source[4],packed[2],out[4],saved[4];
 uint32_t expected[2],decoded[4]={0};unsigned i;
 if(!xe_transfer_uint_color_prepare(&c,format))return 0;
 e.emit=emit;e.context=&m;
 p.halves=literal(&m,c.halves);p.wide=literal(&m,c.wide);p.shift16=literal(&m,16);
 for(i=0;i<4;++i)source[i]=literal(&m,input[i]);
 for(i=0;i<4;++i)saved[i]=0xa533u;
 packed[0]=saved[0];packed[1]=saved[1];
 m.attempts=0;m.fail_at=phase==1?fail_at:0;
 if(!xe_transfer_uint_color_pack(&e,&p,source,packed))
  return phase==1 && packed[0]==saved[0] && packed[1]==saved[1];
 if(format<14) {
  expected[0]=(input[0]&65535)|((input[1]&65535)<<16);
  expected[1]=(format==5 || format==7)?(input[2]&65535)|((input[3]&65535)<<16):0;
 } else {expected[0]=input[0];expected[1]=format==15?input[1]:0;}
 if(m.values[packed[0]]!=expected[0] || m.values[packed[1]]!=expected[1])return 0;
 /* Independent packed input with deliberately nonzero unused second word. */
 m.fail_at=0;packed[0]=literal(&m,input[2]);packed[1]=literal(&m,input[3]);
 for(i=0;i<4;++i)out[i]=saved[i];
 m.attempts=0;m.fail_at=phase==2?fail_at:0;
 if(!xe_transfer_uint_color_unpack(&e,&p,packed,out))
  return phase==2 && !memcmp(out,saved,sizeof(out));
 if(format<14) {
  decoded[0]=input[2]&65535;decoded[1]=input[2]>>16;
  if(format==5 || format==7){decoded[2]=input[3]&65535;decoded[3]=input[3]>>16;}
 } else {decoded[0]=input[2];if(format==15)decoded[1]=input[3];}
 for(i=0;i<4;++i)if(m.values[out[i]]!=decoded[i])return 0;
 return 1;
}
int main(void) {
 static const unsigned formats[]={4,5,6,7,14,15};
 static const uint32_t edges[]={0,1,65535,65536,0x8000u,0xffff8000u,0x7c007e01u,0xff800001u,0x80000000u,0xffffffffu};
 xe_transfer_uint_color_config config,saved;uint32_t input[4],seed=0x137ac1u;
 unsigned f,i,lane,fail,phase;
 memset(&config,0xa5,sizeof(config));saved=config;
 for(i=0;i<32;++i) {
  int valid=i==4 || i==5 || i==6 || i==7 || i==14 || i==15;
  if(xe_transfer_uint_color_prepare(&config,i)!=valid)return 1;
  if(!valid && memcmp(&config,&saved,sizeof(config)))return 1;
  config=saved;
 }
 for(f=0;f<6;++f)for(i=0;i<4096;++i) {
  for(lane=0;lane<4;++lane){seed=seed*1664525u+1013904223u;input[lane]=i<10?edges[(i+lane)%10]:seed;}
  if(!check(formats[f],input,0,0))return 1;
 }
 for(fail=1;fail<24;++fail)for(phase=1;phase<=2;++phase)
  if(!check(5,input,fail,phase))return 1;
 puts("PASS: 24576 integer-view pack/unpack records, independent payloads, metadata/failure guards");return 0;
}
