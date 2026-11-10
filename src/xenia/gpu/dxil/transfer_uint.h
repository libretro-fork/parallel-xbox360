/* Initialization-only unsigned ownership-transfer image shader. BSD; C89. */
#ifndef XE_DXIL_TRANSFER_UINT_H
#define XE_DXIL_TRANSFER_UINT_H
#include "libretro/core_transfer_uint.h"
#include "shader_module.h"
#ifdef __cplusplus
extern "C" {
#endif
/* t0/space0: unsigned non-array 2D SS/MS image.
 * b1/space31: xe_transfer_uint_packet, output SV_Target[options->target].
 * The caller must prepare matching attachment format, coverage and lifetimes.
 * No pipeline creation, command submission or file I/O in this function. */
int xe_dxil_transfer_uint_shader(const xe_transfer_uint_options *options,
 XeDxilShaderBinary *out);
#ifdef __cplusplus
}
#endif
#endif
