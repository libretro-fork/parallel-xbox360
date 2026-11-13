/* Compact routing codec against independent legacy routes. BSD; MSVC C89. */
#define main existing_address_test_main
#include "core_transfer_address_test.c"
#undef main
#include "libretro/core_transfer_address_packet.h"
#include <stdlib.h>
#define PACKET_CHECK(x) do {if(!(x)){fprintf(stderr,"Address packet line%u\n",(unsigned)__LINE__);exit(1);}}while(0)
static unsigned packet_cases,packet_failure_points;
typedef struct packet_machine {machine raw;unsigned char boolean[512];} packet_machine;
static uintptr_t packet_emit(void *opaque,enum xe_transfer_sample_op op,uintptr_t a,uintptr_t b,uintptr_t c) {
 packet_machine *typed=(packet_machine*)opaque;machine *m=&typed->raw;uintptr_t result;
 if(op!=XE_TRANSFER_SAMPLE_CONST){
  if(!a || !b || a>m->count || b>m->count)return 0;
  if(op==XE_TRANSFER_SAMPLE_SELECT){
   if(!c || c>m->count || !typed->boolean[a] || typed->boolean[b] || typed->boolean[c])return 0;
  }else if(typed->boolean[a] || typed->boolean[b])return 0;
 }
 result=emit(m,op,a,b,c);
 if(result)typed->boolean[result]=(unsigned char)(op==XE_TRANSFER_SAMPLE_EQ || op==XE_TRANSFER_SAMPLE_GE);
 return result;
}
static uintptr_t packet_word(machine *m,const xe_transfer_address_packet *packet,unsigned index) {
 uint32_t value;
 switch(index){case 0:value=packet->address;break;case 1:value=packet->geometry;break;
  case 2:value=packet->dest_reciprocal_x;break;case 3:value=packet->dest_reciprocal_y;break;
  case 4:value=packet->source_pitch_reciprocal;break;default:value=packet->sample;break;}
 return literal(m,value);
}
static unsigned packet_check_route(const xe_transfer_address_packet_config *config,
 const xe_transfer_address_key *key,unsigned sample,uint32_t x,uint32_t y,unsigned failure) {
 packet_machine typed={0};machine *m=&typed.raw;xe_transfer_address_packet packet,other;
 xe_transfer_address_packet_source writer;xe_transfer_address_config expected_config;
 xe_transfer_address_parameters parameters,saved;xe_transfer_address_result result;
 xe_transfer_sample_emitter e;uintptr_t ids[XE_AP_WORDS],ix,iy;uint32_t expected[10];unsigned i,attempts;
 PACKET_CHECK(xe_transfer_address_packet_prepare(&packet,config,key,sample));
 writer.config=config;writer.key=key;writer.sample=sample;
 PACKET_CHECK(xe_transfer_address_packet_write(&other,sizeof(other),&writer));PACKET_CHECK(!memcmp(&packet,&other,sizeof(packet)));
 PACKET_CHECK(xe_transfer_address_prepare(&expected_config,key));
 e.emit=packet_emit;e.context=&typed;for(i=0;i<XE_AP_WORDS;++i)ids[i]=packet_word(m,&packet,i);
 memset(&parameters,0xa5,sizeof(parameters));saved=parameters;m->attempts=0;m->fail_at=failure;
 if(!xe_transfer_address_packet_emit(&e,config,ids,&parameters)){
  PACKET_CHECK(failure && !memcmp(&parameters,&saved,sizeof(parameters)));++packet_failure_points;return 0;
 }
 PACKET_CHECK(!failure);attempts=m->attempts;
 for(i=0;i<XE_ADDR_WORDS;++i)PACKET_CHECK(!typed.boolean[parameters.word[i]] && m->value[parameters.word[i]]==expected_config.word[i]);
 ix=literal(m,x);iy=literal(m,y);
 PACKET_CHECK(xe_transfer_address_emit(&e,&parameters,ix,iy,ids[XE_AP_SAMPLE],&result));
 legacy(key,x,y,sample,expected);
#define PACKET_MATCH(field,index) PACKET_CHECK(!typed.boolean[result.field] && m->value[result.field]==expected[index])
 PACKET_MATCH(x,0);PACKET_MATCH(y,1);PACKET_MATCH(sample,2);PACKET_MATCH(second_x,3);
 PACKET_MATCH(half,4);PACKET_MATCH(two_loads,5);PACKET_MATCH(tile,6);
 PACKET_MATCH(dest_tile,7);PACKET_MATCH(dest_x,8);PACKET_MATCH(dest_y,9);
#undef PACKET_MATCH
 ++packet_cases;return attempts;
}
static void packet_rejection(void) {
 xe_transfer_address_packet_config config={4,4,1},bad_config;
 xe_transfer_address_key key={0},bad;xe_transfer_address_packet packet,saved;
 xe_transfer_address_packet_source writer;unsigned i;
 key.dest_color=key.source_color=1;key.dest_native2=key.source_native2=1;
 key.dest_scale_x=key.dest_scale_y=key.source_scale_x=key.source_scale_y=4;key.dest_pitch=key.source_pitch=1;
 memset(&packet,0xa5,sizeof(packet));saved=packet;
 for(i=0;i<12;++i){bad=key;bad_config=config;
  switch(i){case 0:bad.source_pitch=0;break;case 1:bad.dest_pitch=1024;break;case 2:bad.dest_to_source_base=-2048;break;
   case 3:bad.dest_msaa=3;break;case 4:bad.source_native2=0;break;case 5:bad.dest_scale_x=2;break;
   case 6:bad.dest_color=0;bad.dest_wide=1;break;case 7:bad_config.scale_x=0;break;
   case 8:bad_config.native2=2;break;case 9:bad.source_msaa=3;break;case 10:bad_config.scale_x=UINT32_MAX;break;
   default:bad.dest_to_source_base=2048;break;}
  PACKET_CHECK(!xe_transfer_address_packet_prepare(&packet,&bad_config,&bad,0));PACKET_CHECK(!memcmp(&packet,&saved,sizeof(packet)));
 }
 PACKET_CHECK(!xe_transfer_address_packet_prepare(&packet,&config,&key,1));PACKET_CHECK(!memcmp(&packet,&saved,sizeof(packet)));
 PACKET_CHECK(!xe_transfer_address_packet_prepare(NULL,&config,&key,0));
 PACKET_CHECK(!xe_transfer_address_packet_prepare(&packet,NULL,&key,0));
 PACKET_CHECK(!xe_transfer_address_packet_prepare(&packet,&config,NULL,0));
 writer.config=&config;writer.key=&key;writer.sample=0;
 PACKET_CHECK(!xe_transfer_address_packet_write(&packet,sizeof(packet)-1,&writer));
 PACKET_CHECK(!xe_transfer_address_packet_write(&packet,sizeof(packet),NULL));
 PACKET_CHECK(!memcmp(&packet,&saved,sizeof(packet)));
}
static void packet_ir_rejection(void) {
 machine m={0};xe_transfer_sample_emitter e,invalid;
 xe_transfer_address_packet_config config={4,4,1},bad={0,4,1};
 xe_transfer_address_parameters parameters,saved;uintptr_t ids[XE_AP_WORDS];unsigned i;
 e.emit=emit;e.context=&m;invalid=e;invalid.emit=NULL;
 for(i=0;i<XE_AP_WORDS;++i)ids[i]=literal(&m,0);
 memset(&parameters,0xa5,sizeof(parameters));saved=parameters;
 PACKET_CHECK(!xe_transfer_address_packet_emit(NULL,&config,ids,&parameters));
 PACKET_CHECK(!xe_transfer_address_packet_emit(&invalid,&config,ids,&parameters));
 PACKET_CHECK(!xe_transfer_address_packet_emit(&e,NULL,ids,&parameters));
 PACKET_CHECK(!xe_transfer_address_packet_emit(&e,&bad,ids,&parameters));
 PACKET_CHECK(!xe_transfer_address_packet_emit(&e,&config,NULL,&parameters));
 PACKET_CHECK(!xe_transfer_address_packet_emit(&e,&config,ids,NULL));
 for(i=0;i<XE_AP_WORDS;++i){uintptr_t saved_id=ids[i];ids[i]=0;
  PACKET_CHECK(!xe_transfer_address_packet_emit(&e,&config,ids,&parameters));ids[i]=saved_id;
 }
 PACKET_CHECK(!memcmp(&parameters,&saved,sizeof(parameters)));
}
int main(void) {
 static const unsigned scales[4][2]={{1,1},{4,4},{3,5},{16,16}};
 static const unsigned pitches[4]={1,3,17,1023};static const int delta[4]={-2047,-7,0,2047};
 xe_transfer_address_packet_config config;xe_transfer_address_key key={0};
 unsigned n,g,d,s,dk,sk,c,k,sample=0,point,attempts=0,i;uint32_t dw,dh,x=0,y=0;
 packet_rejection();packet_ir_rejection();
 for(n=0;n<2;++n)for(g=0;g<4;++g)for(d=0;d<3;++d)for(s=0;s<3;++s)
 for(dk=0;dk<3;++dk)for(sk=0;sk<3;++sk)for(c=0;c<4;++c){
  config.scale_x=scales[g][0];config.scale_y=scales[g][1];config.native2=n;
  key.dest_native2=key.source_native2=n;key.dest_msaa=d;key.source_msaa=s;
  key.dest_color=dk!=0;key.source_color=sk!=0;key.dest_wide=dk==2;key.source_wide=sk==2;
  key.dest_scale_x=c&1?1:config.scale_x;key.dest_scale_y=c&1?1:config.scale_y;
  key.source_scale_x=c&2?1:config.scale_x;key.source_scale_y=c&2?1:config.scale_y;
  key.dest_pitch=pitches[(g+dk+c)&3];key.source_pitch=pitches[(n+sk+s)&3];key.dest_to_source_base=delta[(g+d+s+c)&3];
  dw=(80*key.dest_scale_x)>>(key.dest_wide+(d==2));dh=(16*key.dest_scale_y)>>(d!=0);
  for(k=0;k<(1u<<d);++k){sample=d==1 && !n?k*3:k;
   for(point=0;point<3;++point){x=point==0?0:point==1?key.dest_pitch*dw-1:127*dw+dw/2;y=point==0?0:point==1?3*dh-1:127*dh+dh/2;
    attempts=packet_check_route(&config,&key,sample,x,y,0);
   }
  }
 }
 PACKET_CHECK(attempts && packet_cases==18144);
 for(i=1;i<=attempts;++i)packet_check_route(&config,&key,sample,x,y,i);
 PACKET_CHECK(packet_failure_points==attempts);
 printf("PASS: %u compact24-byte routing cases against legacy geometry, %u emitter failures, direct writers and rejection guards\n",packet_cases,packet_failure_points);
 return 0;
}
