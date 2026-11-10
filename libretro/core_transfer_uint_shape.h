/* Initialization-known integer transfer fields. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_UINT_SHAPE_H
#define XE_CORE_TRANSFER_UINT_SHAPE_H
#include "core_transfer_uint.h"
typedef struct xe_transfer_uint_shape {
 xe_transfer_address_config address;
 xe_transfer_uint_color_config dest;
 uint32_t mask;
} xe_transfer_uint_shape;
/* Source width/codec, pitches, source-pitch reciprocal, base delta and manual
 * sample index remain runtime data. The caller must select a prepared shader
 * whose sample/scale/destination-view fields agree with its checked packet.
 * Rejection leaves out unchanged. Inputs are borrowed and must not overlap it. */
static INLINE int xe_transfer_uint_shape_prepare(xe_transfer_uint_shape *out,
 const xe_transfer_uint_options *options,const xe_transfer_address_key *key,
 unsigned dest_format) {
 xe_transfer_uint_color_config dest;
 if(!out || !options || !key || options->target>3 ||
  options->source_multisampled>1 || options->sample_mode>XE_TRANSFER_UINT_SAMPLE_CONSTANT ||
  key->source_color!=1 || key->dest_color!=1 ||
  options->source_multisampled!=(unsigned)(key->source_msaa!=0) ||
  (options->sample_mode==XE_TRANSFER_UINT_SINGLE_SAMPLE)!=(key->dest_msaa==0) ||
  !xe_transfer_uint_color_prepare(&dest,dest_format) ||
  key->dest_wide!=(unsigned)(dest.wide!=0) ||
  !xe_transfer_address_prepare(&out->address,key))return 0;
 out->dest=dest;
 out->mask=UINT32_C(0x3ff)|(UINT32_C(1)<<XE_ADDR_D_WIDE)|
  (UINT32_C(15)<<XE_ADDR_D_TW)|(UINT32_C(1)<<XE_ADDR_S_TH)|
  (UINT32_C(1)<<XE_ADDR_SWAP)|(UINT32_C(1)<<25)|(UINT32_C(1)<<26);
 return 1;
}
/* Only call for a set mask bit. No cross-member array traversal or aliasing. */
static INLINE uint32_t xe_transfer_uint_shape_word(const xe_transfer_uint_shape *shape,
 unsigned word) {
 return word<XE_ADDR_WORDS?shape->address.word[word]:
  word==25?shape->dest.halves:shape->dest.wide;
}
#endif
