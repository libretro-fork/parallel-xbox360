/* Native depth-only EDRAM pixel shaders. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_ROV_DEPTH_H
#define XE_GPU_DXIL_ROV_DEPTH_H
#include "shader_module.h"
typedef struct XeDxilRovDepthOptions {
  unsigned msaa, scale_x, scale_y, full_counters, survey;
  unsigned system_constants_bytes, guest_linkage;
  unsigned flags_word, pitch_word, depth_base_word, counter_word;
  unsigned front_scale_word, back_scale_word, front_offset_word, back_offset_word;
  unsigned front_masks_word, back_masks_word, front_ops_word, back_ops_word;
  unsigned depth_stencil_mask, float24_mask, depth_write_mask, stencil_mask, depth_function_shift;
} XeDxilRovDepthOptions;
#ifdef __cplusplus
extern "C" {
#endif
/* D3D12 maps guest 2x coverage to host 4x samples 0 and 3. */
int xe_dxil_build_rov_depth(const XeDxilRovDepthOptions *options, XeDxilShaderBinary *binary);
/* Zero-initialized storage, indexed by guest MSAA and survey selection. */
int xe_dxil_build_rov_depth_set(const XeDxilRovDepthOptions *base, XeDxilShaderBinary binaries[3][2]);
void xe_dxil_rov_depth_set_free(XeDxilShaderBinary binaries[3][2]);
#ifdef __cplusplus
}
#endif
#endif
