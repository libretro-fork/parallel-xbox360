/* Initialization-only native DXIL numeric identity shader. BSD; C89. */
#ifndef XE_DXIL_TRANSFER_IDENTITY_H
#define XE_DXIL_TRANSFER_IDENTITY_H
#include "libretro/core_transfer_identity.h"
#include "shader_module.h"
#ifdef __cplusplus
extern "C" {
#endif
/* t0/space0 float SS/MS texture; b1/space31 packet20bytes; float4 target.
 * Caller must match shape, actual view class, sample coverage and lifetimes.
 * No HLSL/source compiler, pipeline creation, file I/O or command submission. */
int xe_dxil_transfer_identity_shader(const xe_transfer_identity_options *options,
 const xe_transfer_address_key *key,XeDxilShaderBinary *out);
#ifdef __cplusplus
}
#endif
#endif
