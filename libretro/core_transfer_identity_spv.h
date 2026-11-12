/* Initialization-only numeric identity image shader. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_IDENTITY_SPV_H
#define XE_CORE_TRANSFER_IDENTITY_SPV_H
#include "core_transfer_identity.h"
#include "core_spv_binary.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Set0/binding0: float-sampled SS/MS non-array2D image. Output float4.
 * Push packet20bytes. No arithmetic on fetched color, pack/unpack, staging,
 * source compiler or image alias. Builtin sample mode requires sample shading;
 * constant mode requires one correctly masked PSO per physical sample.
 * CPU words belong to out, with its normal failure/reset ownership. */
int xe_transfer_identity_spv_build(xe_spv_binary *out,
 const xe_transfer_identity_options *options,const xe_transfer_address_key *key);
#ifdef __cplusplus
}
#endif
#endif
