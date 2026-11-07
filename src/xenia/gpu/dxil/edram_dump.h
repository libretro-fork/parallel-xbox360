/* Native host render target to EDRAM dump generation. MSVC C89. */
#ifndef XE_GPU_DXIL_EDRAM_DUMP_H
#define XE_GPU_DXIL_EDRAM_DUMP_H
#include "shader_module.h"
typedef struct XeDxilEdramDumpOptions {
  unsigned format, depth, msaa;
  unsigned scale_x, scale_y, native_layout, native_source;
  unsigned source_uint, msaa_2x_supported;
  unsigned round_depth, depth_converted_in_pixel_shader;
  unsigned destination_space, source_space;
  unsigned direct_resolve;
  unsigned direct_resolve_3d;
} XeDxilEdramDumpOptions;
#ifdef __cplusplus
extern "C" {
#endif
int xe_dxil_build_edram_dump(const XeDxilEdramDumpOptions *options, XeDxilShaderBinary *binary);
#ifdef __cplusplus
}
#endif
#endif
