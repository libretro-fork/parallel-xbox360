/* Prepared numeric-view identity transfers. BSD; MSVC C89.
 * Preserve the existing typed views and their compression opportunities.
 * This family never changes numeric representation, gamma or guest width. */
#ifndef XE_CORE_TRANSFER_IDENTITY_H
#define XE_CORE_TRANSFER_IDENTITY_H
#include <stddef.h>
#include "core_transfer_address.h"

enum xe_transfer_identity_sample_mode {
 XE_TRANSFER_IDENTITY_SINGLE_SAMPLE,
 XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN,
 XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT
};
enum xe_transfer_identity_view {
 XE_TRANSFER_IDENTITY_RGBA8,
 XE_TRANSFER_IDENTITY_RGBA16,
 XE_TRANSFER_IDENTITY_RGB10A2,
 XE_TRANSFER_IDENTITY_RGBA16_FLOAT,
 XE_TRANSFER_IDENTITY_VIEWS
};
typedef struct xe_transfer_identity_options {
 unsigned source_multisampled,sample_mode,target,view;
} xe_transfer_identity_options;
typedef struct xe_transfer_identity_packet {
 uint32_t dest_pitch,source_pitch,source_reciprocal,base_delta,sample;
} xe_transfer_identity_packet;
typedef char xe_transfer_identity_packet_size[sizeof(xe_transfer_identity_packet)==20?1:-1];
typedef char xe_transfer_identity_sample_offset[offsetof(xe_transfer_identity_packet,sample)==16?1:-1];

/* Physical supported counts encoded as bits1/2/4; guest enums encoded as
 * bits0/1/2. Other physical counts are unused. Native2 is a device-wide
 * policy: emulated guest2 uses physical4, including sample positions0/3. */
static INLINE unsigned xe_transfer_identity_samples(unsigned physical,unsigned native2)
{
 if(native2>1)return 0;
 return (physical&1?1u:0u)|(physical&(native2?2u:4u)?2u:0u)|(physical&4?4u:0u);
}

/* Guest aliases10/12 use the same stored layout as2/3. Format1 remains
 * distinct from0 even when both use RGBA8: gamma conversion is not identity.
 * Half/integer-view families4..7,14/15 belong to the existing raw bank.
 * Failure preserves *view. Backend must confirm the actual image view. */
static INLINE int xe_transfer_identity_class(unsigned source,unsigned dest,
 unsigned gamma16,unsigned *view)
{
 unsigned value;
 if(!view || gamma16>1)return 0;
 if(source==10)source=2;else if(source==12)source=3;
 if(dest==10)dest=2;else if(dest==12)dest=3;
 if(source!=dest)return 0;
 switch(dest) {
  case 0:value=XE_TRANSFER_IDENTITY_RGBA8;break;
  case 1:value=gamma16?XE_TRANSFER_IDENTITY_RGBA16:XE_TRANSFER_IDENTITY_RGBA8;break;
  case 2:value=XE_TRANSFER_IDENTITY_RGB10A2;break;
  case 3:value=XE_TRANSFER_IDENTITY_RGBA16_FLOAT;break;
  default:return 0;
 }
 *view=value;return 1;
}
static INLINE int xe_transfer_identity_key_valid(
 const xe_transfer_identity_options *options,const xe_transfer_address_key *key)
{
 return options && key && options->target<4 && options->view<XE_TRANSFER_IDENTITY_VIEWS &&
  options->source_multisampled<=1 && options->sample_mode<=XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT &&
  options->source_multisampled==(unsigned)(key->source_msaa!=0) &&
  (options->sample_mode==XE_TRANSFER_IDENTITY_SINGLE_SAMPLE)==(key->dest_msaa==0) &&
  key->source_color==1 && key->dest_color==1 && !key->source_wide && !key->dest_wide;
}
/* Inputs are immutable borrowed metadata, disjoint from out. Write final
 * unpublished command storage directly. Invalid input leaves it unchanged.
 * Native/emulated2x sample indices use the physical destination sample. */
static INLINE int xe_transfer_identity_packet_prepare(xe_transfer_identity_packet *out,
 const xe_transfer_identity_options *options,const xe_transfer_address_key *key,
 unsigned source_format,unsigned dest_format,unsigned gamma16,uint32_t sample)
{
 unsigned view;
 if(!out || !xe_transfer_identity_key_valid(options,key) || !xe_transfer_address_key_valid(key) ||
  !xe_transfer_identity_class(source_format,dest_format,gamma16,&view) || view!=options->view)return 0;
 if(key->dest_msaa==0?sample!=0:key->dest_msaa==1?
  (key->dest_native2?sample>1:(sample!=0 && sample!=3)):sample>3)return 0;
 out->dest_pitch=key->dest_pitch;out->source_pitch=key->source_pitch;
 xe_gpu_divide_prepare(key->source_pitch,&out->source_reciprocal);
 out->base_delta=(uint32_t)key->dest_to_source_base;out->sample=sample;return 1;
}
typedef struct xe_transfer_identity_packet_source {
 const xe_transfer_identity_options *options;
 const xe_transfer_address_key *key;
 unsigned source_format,dest_format,gamma16;
 uint32_t sample;
} xe_transfer_identity_packet_source;
static INLINE int xe_transfer_identity_packet_write(void *destination,size_t bytes,void *context)
{
 const xe_transfer_identity_packet_source *source=(const xe_transfer_identity_packet_source*)context;
 if(!source || bytes!=sizeof(xe_transfer_identity_packet))return 0;
 return xe_transfer_identity_packet_prepare((xe_transfer_identity_packet*)destination,
  source->options,source->key,source->source_format,source->dest_format,source->gamma16,source->sample);
}
typedef struct xe_transfer_identity_shape {
 xe_transfer_address_config address;
 uint32_t mask;
} xe_transfer_identity_shape;
/* Only the five runtime words have packet storage; fixed words never cross
 * the command stream. Call for an unset shape bit, or23 for manual sample. */
static INLINE unsigned xe_transfer_identity_parameter_word(unsigned word)
{
 return word==XE_ADDR_D_PITCH?0:word==XE_ADDR_S_PITCH?1:
  word==XE_ADDR_S_PR?2:word==XE_ADDR_DELTA?3:4;
}
/* Pitches, reciprocal source pitch and base delta are runtime data. All
 * image sample/scale/layout fields are known at initialization. The prepared
 * pipeline must match these fields and the actual numeric attachment class.
 * Reject before modifying out; do not overlap key/options with out. */
static INLINE int xe_transfer_identity_shape_prepare(xe_transfer_identity_shape *out,
 const xe_transfer_identity_options *options,const xe_transfer_address_key *key)
{
 if(!out || !xe_transfer_identity_key_valid(options,key) ||
  !xe_transfer_address_prepare(&out->address,key))return 0;
 out->mask=((UINT32_C(1)<<XE_ADDR_WORDS)-1)&
  ~((UINT32_C(1)<<XE_ADDR_D_PITCH)|(UINT32_C(1)<<XE_ADDR_S_PITCH)|
    (UINT32_C(1)<<XE_ADDR_S_PR)|(UINT32_C(1)<<XE_ADDR_DELTA));
 return 1;
}
#endif
