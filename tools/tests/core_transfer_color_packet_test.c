/* Typed view/sample admission and exact final command packet. MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_transfer_color.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"color packet line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static unsigned canonical(unsigned format)
{
 static const unsigned map[16]={0,1,2,3,4,5,4,5,99,99,2,99,3,99,14,15};
 return format<16?map[format]:99;
}
static void admission(void)
{
 static const uint32_t scales[3][2]={{1,1},{3,5},{4,4}};
 struct {uint32_t before;xe_transfer_color_packet packet;uint32_t after;} storage,saved;
 xe_transfer_address_key key;
 xe_transfer_address_packet_config config;
 xe_transfer_color_options options;
 unsigned g,n,sm,dm,s,d,si,di,mode,source,dest,valid,got,count=0;
 memset(&key,0,sizeof(key));key.source_color=key.dest_color=1;
 key.source_pitch=31;key.dest_pitch=17;key.dest_to_source_base=-2047;
 memset(&options,0,sizeof(options));
 for(g=0;g<3;++g)for(n=0;n<2;++n)for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)
 for(s=0;s<16;++s)for(d=0;d<16;++d)for(si=0;si<2;++si)for(di=0;di<2;++di){
  source=canonical(s);dest=canonical(d);
  config.scale_x=scales[g][0];config.scale_y=scales[g][1];config.native2=n;
  key.source_native2=key.dest_native2=n;key.source_msaa=sm;key.dest_msaa=dm;
  key.source_scale_x=key.dest_scale_x=config.scale_x;key.source_scale_y=key.dest_scale_y=config.scale_y;
  key.source_wide=source==5 || source==15;key.dest_wide=dest==5 || dest==15;
  options.source_uint=si;options.dest_uint=di;options.source_multisampled=sm!=0;
  options.sample_mode=mode=dm?XE_TRANSFER_COLOR_SAMPLE_BUILTIN:XE_TRANSFER_COLOR_SINGLE;
  memset(&storage,0xa5,sizeof(storage));saved=storage;
  valid=source!=99 && dest!=99 && si==(unsigned)(source>=4) && di==(unsigned)(dest>=4);
  got=(unsigned)xe_transfer_color_packet_prepare(&storage.packet,&options,&config,&key,s,d,0);
  CHECK(got==valid);
  CHECK(storage.before==saved.before && storage.after==saved.after);
  if(valid){
   CHECK(storage.packet.source_codec==source && storage.packet.dest_codec==dest);
   CHECK(storage.packet.address.address==UINT32_C(0x80107c11));
   CHECK(storage.packet.address.source_pitch_reciprocal==UINT32_C(0x08421084));
   CHECK(storage.packet.address.sample==0);++count;
  }else CHECK(!memcmp(&storage,&saved,sizeof(storage)));
  if(mode==XE_TRANSFER_COLOR_SAMPLE_BUILTIN){
   saved=storage;CHECK(!xe_transfer_color_packet_prepare(&storage.packet,&options,&config,&key,s,d,1));
   CHECK(!memcmp(&storage,&saved,sizeof(storage)));
  }
 }
 CHECK(count==7776);
 printf("PASS: %u typed color packets, all12 source/dest formats and view rejection, scale/MSAA policies, untouched guards and failed publication\n",count);
}
static void exact_packet(void)
{
 static const uint32_t expected[8]={0x80107c11,0xc0,0x01111111,0x03333333,0x08421084,0,0,1};
 xe_transfer_color_packet packet,saved;
 xe_transfer_color_options options={0,0,0,XE_TRANSFER_COLOR_SINGLE,0};
 xe_transfer_address_packet_config config={3,5,1};
 xe_transfer_address_key key;
 xe_transfer_color_packet_source borrowed;
 memset(&key,0,sizeof(key));key.dest_native2=key.source_native2=1;
 key.dest_scale_x=key.source_scale_x=3;key.dest_scale_y=key.source_scale_y=5;
 key.dest_color=key.source_color=1;key.dest_pitch=17;key.source_pitch=31;key.dest_to_source_base=-2047;
 CHECK(xe_transfer_color_packet_prepare(&packet,&options,&config,&key,0,1,0));
 CHECK(!memcmp(&packet,expected,sizeof(packet)));
 borrowed.options=&options;borrowed.config=&config;borrowed.key=&key;
 borrowed.source_format=0;borrowed.dest_format=1;borrowed.sample=0;
 CHECK(xe_transfer_color_packet_write(&packet,sizeof(packet),&borrowed));saved=packet;
 CHECK(!xe_transfer_color_packet_write(&packet,sizeof(packet)-1,&borrowed));
 CHECK(!memcmp(&packet,&saved,sizeof(packet)));
 options.sample_mode=XE_TRANSFER_COLOR_SAMPLE_CONSTANT;key.dest_msaa=1;key.dest_native2=key.source_native2=config.native2=0;
 CHECK(xe_transfer_color_packet_prepare(&packet,&options,&config,&key,0,1,3));saved=packet;
 CHECK(!xe_transfer_color_packet_prepare(&packet,&options,&config,&key,0,1,1));CHECK(!memcmp(&packet,&saved,sizeof(packet)));
 options.target=4;CHECK(!xe_transfer_color_packet_prepare(&packet,&options,&config,&key,0,1,3));
 CHECK(!memcmp(&packet,&saved,sizeof(packet)));
 puts("PASS: literal32-byte packet, direct writer lease, emulated2x physical sample3 and atomic failure");
}
static void packed_packet(void){
 unsigned char bytes[9],before[9];uint32_t input=UINT32_C(0xabc01234),output;
 memset(bytes,0x59,sizeof(bytes));memcpy(before,bytes,sizeof(bytes));
 CHECK(!xe_transfer_packed_address_write(bytes+1,3,&input));
 CHECK(!memcmp(bytes,before,sizeof(bytes)));
 CHECK(!xe_transfer_packed_address_write(NULL,4,&input));
 CHECK(!xe_transfer_packed_address_write(bytes+1,4,NULL));
 CHECK(xe_transfer_packed_address_write(bytes+1,4,&input));
 memcpy(&output,bytes+1,4);CHECK(output==input && bytes[0]==0x59 && bytes[5]==0x59);
}
int main(void){admission();exact_packet();packed_packet();return 0;}
