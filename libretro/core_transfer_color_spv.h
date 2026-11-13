/* Initialization-only generic typed color image shader. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_COLOR_SPV_H
#define XE_CORE_TRANSFER_COLOR_SPV_H
#include "core_transfer_color.h"
#include "core_spv_binary.h"
#ifdef __cplusplus
extern "C" {
#endif
/* One borrowed sampled image at set0/binding0, final32-byte push packet,
 * uint4 or float4 output at target. Sample shading/coverage and compatible
 * attachment/view formats remain the pipeline bank's responsibility.
 * Invalid CPU metadata must never be published to a command stream. */
#ifdef XE_TRANSFER_COLOR_DYNAMIC_TEST
int xe_transfer_color_spv_build(xe_spv_binary *,const xe_transfer_color_options *,
 const xe_transfer_address_packet_config *);
#endif
/* Initialization-known single-sample format/layout. Only pitches, reciprocal
 * source pitch and base delta remain dynamic. The runtime packet stays32B.
 * Plan exact final capacity without allocating or writing a word stream. */
int xe_transfer_color_spv_shape_words(size_t *,const xe_transfer_color_options *,
 const xe_transfer_address_packet_config *,const xe_transfer_address_key *,unsigned,unsigned);
int xe_transfer_color_spv_build_shape(xe_spv_binary *,const xe_transfer_color_options *,
 const xe_transfer_address_packet_config *,const xe_transfer_address_key *,unsigned,unsigned);
#ifdef __cplusplus
}
#endif
#endif
