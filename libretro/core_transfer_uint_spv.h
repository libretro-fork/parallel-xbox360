/* Prepared integer ownership-transfer fragment shader. BSD; MSVC C89.
 * Construction is initialization-only. Backend integer views are required;
 * guest format numbers alone do not establish compatible image views. */
#ifndef XE_CORE_TRANSFER_UINT_SPV_H
#define XE_CORE_TRANSFER_UINT_SPV_H
#include "core_transfer_uint.h"
#include "core_spv_binary.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Set0/binding0: unsigned sampled 2D image (SS or MS, non-array).
 * Push constants: xe_transfer_uint_packet. Output: uint4 at target location.
 * Builtin sample mode requires sampleRateShading, minSampleShading=1 and the
 * correct destination coverage mask. Constant mode requires a matching
 * per-sample PSO coverage mask; it does not broadcast one sample to all others.
 * Returned words are owned by out; no GLSL, source compiler or image staging. */
int xe_transfer_uint_spv_build(xe_spv_binary *out,
 const xe_transfer_uint_options *options);
/* Initialization-known sample/scale/destination view. Pitches, source codec
 * and base delta remain runtime data in the unchanged 112-byte packet. */
int xe_transfer_uint_spv_build_shape(xe_spv_binary *out,
 const xe_transfer_uint_options *options,const xe_transfer_address_key *key,
 unsigned dest_format);
#ifdef __cplusplus
}
#endif
#endif
