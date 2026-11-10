/* Checked interface identity and direct final metadata. BSD; C89. */
#include "libretro/core_transfer_uint.h"
#include <stdio.h>
#include <string.h>
int main(void) {
 static const unsigned formats[]={4,5,6,7,14,15};
 static const unsigned scales[6][4]={{1,1,1,1},{2,2,2,2},{3,5,3,5},{1,1,3,5},{3,5,1,1},{1,1,1,3}};
 xe_transfer_address_key key={0};xe_transfer_uint_options options;
 xe_transfer_uint_packet packet,saved;
 xe_transfer_uint_packet_source writer;
 xe_transfer_address_config address;xe_transfer_uint_color_config source,dest;
 unsigned s,d,dm,sm,n,scale,mode,cases=0;uint32_t sample;
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(s=0;s<6;++s)for(d=0;d<6;++d)for(dm=0;dm<3;++dm)for(sm=0;sm<3;++sm)
 for(n=0;n<2;++n)for(scale=0;scale<6;++scale)for(mode=0;mode<2;++mode) {
  if(!xe_transfer_uint_color_prepare(&source,formats[s]) ||
     !xe_transfer_uint_color_prepare(&dest,formats[d]))return 1;
  key.source_wide=source.wide!=0;key.dest_wide=dest.wide!=0;
  key.dest_msaa=dm;key.source_msaa=sm;key.dest_native2=key.source_native2=n;
  key.dest_scale_x=scales[scale][0];key.dest_scale_y=scales[scale][1];
  key.source_scale_x=scales[scale][2];key.source_scale_y=scales[scale][3];
  options.source_multisampled=sm!=0;options.target=cases%4;
  options.sample_mode=dm?(mode?XE_TRANSFER_UINT_SAMPLE_CONSTANT:XE_TRANSFER_UINT_SAMPLE_BUILTIN):XE_TRANSFER_UINT_SINGLE_SAMPLE;
  sample=dm==2?3:dm==1?(n?1:3):0;
  if(!xe_transfer_uint_packet_prepare(&packet,&options,&key,formats[s],formats[d],sample) ||
     !xe_transfer_address_prepare(&address,&key) ||
     memcmp(&packet.address,&address,sizeof(address)) ||
     memcmp(&packet.source,&source,sizeof(source)) ||
     memcmp(&packet.dest,&dest,sizeof(dest)) || packet.sample!=sample)return 1;
  saved=packet;memset(&packet,0xa5,sizeof(packet));
  writer.options=&options;writer.key=&key;writer.source_format=formats[s];
  writer.dest_format=formats[d];writer.sample=sample;
  if(!xe_transfer_uint_packet_write(&packet,sizeof(packet),&writer) ||
     memcmp(&packet,&saved,sizeof(packet)))return 1;
  if(xe_transfer_uint_packet_write(&packet,sizeof(packet)-4,&writer) ||
     xe_transfer_uint_packet_write(&packet,sizeof(packet)+4,&writer) ||
     xe_transfer_uint_packet_write(&packet,sizeof(packet),NULL) ||
     xe_transfer_uint_packet_write(NULL,sizeof(packet),&writer) ||
     memcmp(&packet,&saved,sizeof(packet)))return 1;
  ++cases;
 }
 memset(&packet,0xa5,sizeof(packet));saved=packet;
#define REJECT(opt,k,s,d,sample) do { \
 if(xe_transfer_uint_packet_prepare(&packet,opt,k,s,d,sample) || \
    memcmp(&packet,&saved,sizeof(packet)))return 1; \
} while(0)
 REJECT(NULL,&key,15,15,0);REJECT(&options,NULL,15,15,0);
 REJECT(&options,&key,8,15,0);REJECT(&options,&key,15,8,0);
 REJECT(&options,&key,4,15,0);REJECT(&options,&key,15,4,0);
 REJECT(&options,&key,15,15,4);
 options.source_multisampled=0;REJECT(&options,&key,15,15,0);
 options.source_multisampled=1;options.sample_mode=XE_TRANSFER_UINT_SINGLE_SAMPLE;
 REJECT(&options,&key,15,15,0);
 options.sample_mode=3;REJECT(&options,&key,15,15,0);
 options.sample_mode=XE_TRANSFER_UINT_SAMPLE_CONSTANT;options.target=4;
 REJECT(&options,&key,15,15,0);
 options.target=0;key.source_color=0;REJECT(&options,&key,15,15,0);
 key.source_color=1;key.source_pitch=0;REJECT(&options,&key,15,15,0);
#undef REJECT
 if(xe_transfer_uint_packet_prepare(NULL,&options,&key,15,15,0))return 1;
 writer.options=&options;writer.key=&key;writer.source_format=writer.dest_format=15;
 writer.sample=0;
 if(xe_transfer_uint_packet_write(&packet,sizeof(packet),&writer) ||
    memcmp(&packet,&saved,sizeof(packet)))return 1;
 for(s=0;s<32;++s)if(xe_transfer_uint_view_format(s)!=(s==6 || s==7?s-2:s))return 1;
 printf("PASS: %u prepared integer interface packets, identity and failure preservation\n",cases);
 return 0;
}
