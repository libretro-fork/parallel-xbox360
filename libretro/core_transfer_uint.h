/* Shared prepared integer transfer packet and interface key. BSD; C89. */
#ifndef XE_CORE_TRANSFER_UINT_H
#define XE_CORE_TRANSFER_UINT_H
#include <stddef.h>
#include "core_transfer_address.h"
#include "core_transfer_uint_color.h"
enum xe_transfer_uint_sample_mode {
 XE_TRANSFER_UINT_SINGLE_SAMPLE,
 XE_TRANSFER_UINT_SAMPLE_BUILTIN,
 XE_TRANSFER_UINT_SAMPLE_CONSTANT
};
typedef struct xe_transfer_uint_options {
 unsigned source_multisampled,sample_mode,target;
} xe_transfer_uint_options;
/* Only for backend-confirmed integer ownership views, never numeric draw
 * attachments: these pairs have identical raw Vk/DXGI storage formats. */
static INLINE unsigned xe_transfer_uint_view_format(unsigned format) {
 return format==6 || format==7?format-2:format;
}


typedef struct xe_transfer_uint_packet {
 xe_transfer_address_config address;
 xe_transfer_uint_color_config source,dest;
 uint32_t sample;
} xe_transfer_uint_packet;
typedef char xe_transfer_uint_packet_size[(sizeof(xe_transfer_uint_packet)==112)?1:-1];
typedef char xe_transfer_uint_source_offset[(offsetof(xe_transfer_uint_packet,source)==92)?1:-1];
typedef char xe_transfer_uint_dest_offset[(offsetof(xe_transfer_uint_packet,dest)==100)?1:-1];
typedef char xe_transfer_uint_sample_offset[(offsetof(xe_transfer_uint_packet,sample)==108)?1:-1];

/* Inputs are borrowed, must not overlap out. Failure leaves out unchanged.
 * sample is a physical destination sample index, including 0/3 for emulated2x.
 * The prepared interface key must agree with the metadata sample layouts.
 * Caller must also match attachment format/location and enabled features. */
static INLINE int xe_transfer_uint_packet_prepare(xe_transfer_uint_packet *out,
 const xe_transfer_uint_options *options,const xe_transfer_address_key *key,
 unsigned source_format,unsigned dest_format,
 uint32_t sample) {
 xe_transfer_uint_color_config source,dest;
 if(!out || !options || !key || options->target>3 ||
    options->source_multisampled>1 || options->sample_mode>XE_TRANSFER_UINT_SAMPLE_CONSTANT ||
    options->source_multisampled!=(unsigned)(key->source_msaa!=0) ||
    (options->sample_mode==XE_TRANSFER_UINT_SINGLE_SAMPLE)!=(key->dest_msaa==0) ||
    key->source_color!=1 || key->dest_color!=1 ||
    !xe_transfer_uint_color_prepare(&source,source_format) ||
    !xe_transfer_uint_color_prepare(&dest,dest_format) ||
    key->source_wide!=(uint32_t)(source.wide!=0) || key->dest_wide!=(uint32_t)(dest.wide!=0))return 0;
 if(key->dest_msaa==0 ? sample!=0 : key->dest_msaa==1 ?
    (key->dest_native2 ? sample>1 : (sample!=0 && sample!=3)) : sample>3)return 0;
 if(!xe_transfer_address_prepare(&out->address,key))return 0;
 out->source=source;out->dest=dest;out->sample=sample;return 1;
}

/* Synchronous destination writer for unpublished command-stream storage.
 * The references expire on return; only the completed packet is retained. */
typedef struct xe_transfer_uint_packet_source {
 const xe_transfer_uint_options *options;
 const xe_transfer_address_key *key;
 unsigned source_format,dest_format;
 uint32_t sample;
} xe_transfer_uint_packet_source;
static INLINE int xe_transfer_uint_packet_write(void *destination,size_t bytes,void *context) {
 const xe_transfer_uint_packet_source *source=(const xe_transfer_uint_packet_source*)context;
 if(!source || bytes!=sizeof(xe_transfer_uint_packet))return 0;
 return xe_transfer_uint_packet_prepare((xe_transfer_uint_packet*)destination,
  source->options,source->key,source->source_format,source->dest_format,source->sample);
}

#endif
