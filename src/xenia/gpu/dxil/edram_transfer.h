/* Native EDRAM ownership transfer generation. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_EDRAM_TRANSFER_H
#define XE_GPU_DXIL_EDRAM_TRANSFER_H
#include "shader_module.h"
typedef struct XeDxilEdramTransferOptions {
  unsigned mode, source_format, destination_format, color_target;
  unsigned source_msaa, destination_msaa, host_depth_msaa;
  unsigned scale_x, scale_y, source_native, destination_native;
  unsigned source_uint, destination_uint, stencil_reference;
  unsigned msaa_2x_supported, round_depth, depth_converted_in_pixel_shader;
  unsigned no_discard_stencil, value_convert;
  unsigned source_space, host_depth_space;
} XeDxilEdramTransferOptions;
#ifdef __cplusplus
extern "C" {
#endif
int xe_dxil_build_edram_transfer(const XeDxilEdramTransferOptions *options, XeDxilShaderBinary *binary);
#ifdef __cplusplus
}
#endif
#endif
