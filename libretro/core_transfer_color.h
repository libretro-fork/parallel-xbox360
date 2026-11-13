/* Prepared generic color-transfer metadata. BSD; MSVC C89.
 * Actual typed image views are required; guest format aliases alone are not
 * sufficient. This does not implement a rendering fallback by itself. */
#ifndef XE_CORE_TRANSFER_COLOR_H
#define XE_CORE_TRANSFER_COLOR_H
#include "core_transfer_address_packet.h"
#include <string.h>
enum xe_transfer_color_sample_mode {
 XE_TRANSFER_COLOR_SINGLE, XE_TRANSFER_COLOR_SAMPLE_BUILTIN,
 XE_TRANSFER_COLOR_SAMPLE_CONSTANT
};
typedef struct xe_transfer_color_options {
 unsigned source_uint,dest_uint,source_multisampled,sample_mode,target;
} xe_transfer_color_options;
typedef struct xe_transfer_color_packet {
 xe_transfer_address_packet address;
 uint32_t source_codec,dest_codec;
} xe_transfer_color_packet;
typedef char xe_transfer_color_packet_size[sizeof(xe_transfer_color_packet)==32?1:-1];
typedef char xe_transfer_color_packet_codec[offsetof(xe_transfer_color_packet,source_codec)==24?1:-1];

/* Normalize only equal stored layouts. Gamma remains distinct: its numeric
 * storage policy must already have been resolved by the backend. */
static INLINE int xe_transfer_color_codec(unsigned format,unsigned integer_view,
 uint32_t *codec) {
 if(!codec || integer_view>1)return 0;
 switch(format){
  case 10:format=2;break;case 12:format=3;break;
  case 6:format=4;break;case 7:format=5;break;default:break;
 }
 if(integer_view){if(format!=4 && format!=5 && format!=14 && format!=15)return 0;}
 else if(format>3)return 0;
 *codec=format;return 1;
}
/* Fill unpublished final command metadata; no pixel storage or staging copy.
 * Inputs are borrowed through return and must not alias out. Failure leaves
 * out intact, including when an incompatible typed view is supplied. */
static INLINE int xe_transfer_color_packet_prepare(xe_transfer_color_packet *out,
 const xe_transfer_color_options *options,const xe_transfer_address_packet_config *config,
 const xe_transfer_address_key *key,unsigned source_format,unsigned dest_format,uint32_t sample) {
 uint32_t source,dest;
 if(!out || !options || !key || options->target>3 || options->source_multisampled>1 ||
    options->sample_mode>XE_TRANSFER_COLOR_SAMPLE_CONSTANT ||
    !xe_transfer_color_codec(source_format,options->source_uint,&source) ||
    !xe_transfer_color_codec(dest_format,options->dest_uint,&dest) ||
    key->source_color!=1 || key->dest_color!=1 ||
    options->source_multisampled!=(unsigned)(key->source_msaa!=0) ||
    (options->sample_mode==XE_TRANSFER_COLOR_SINGLE)!=(key->dest_msaa==0) ||
    key->source_wide!=(unsigned)(source==5 || source==15) ||
    key->dest_wide!=(unsigned)(dest==5 || dest==15) ||
    (options->sample_mode!=XE_TRANSFER_COLOR_SAMPLE_CONSTANT && sample))return 0;
 if(!xe_transfer_address_packet_prepare(&out->address,config,key,sample))return 0;
 out->source_codec=source;out->dest_codec=dest;return 1;
}
typedef struct xe_transfer_color_packet_source {
 const xe_transfer_color_options *options;
 const xe_transfer_address_packet_config *config;
 const xe_transfer_address_key *key;
 unsigned source_format,dest_format;
 uint32_t sample;
} xe_transfer_color_packet_source;
static INLINE int xe_transfer_packed_address_write(void *destination,size_t bytes,void *context) {
 if(!destination || !context || bytes!=sizeof(uint32_t))return 0;
 memcpy(destination,context,sizeof(uint32_t));return 1;
}
static INLINE int xe_transfer_color_packet_write(void *destination,size_t bytes,void *context) {
 const xe_transfer_color_packet_source *source=(const xe_transfer_color_packet_source*)context;
 return source && bytes==sizeof(xe_transfer_color_packet) &&
  xe_transfer_color_packet_prepare((xe_transfer_color_packet*)destination,source->options,
   source->config,source->key,source->source_format,source->dest_format,source->sample);
}
#endif
