/* Native guest pixel stage. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_PIXEL_STAGE_H
#define XE_GPU_DXIL_GUEST_PIXEL_STAGE_H
#include "guest_program.h"
#include "guest_blend.h"
#include "guest_rop.h"
#include "guest_texture_stage.h"
#include "shader_module.h"
typedef struct XeDxilGuestPixelOptions {
  XeDxilGuestProgramOptions program;
  const XeDxilGuestRopOptions *rop;
  const XeDxilTextureStageOptions *textures;
  unsigned point_coordinate_mask, interpolator_mask, centroid_mask, color_mask, writes_depth;
  unsigned system_constants_bytes, flags_word, alpha_reference_word;
  unsigned alpha_to_mask_word, color_bias_word, blend_constant_word;
  unsigned alpha_function_shift, gamma_mask, msaa_shift;
  unsigned depth_mode, blend_rgb_factor, blend_alpha_factor;
  unsigned param_gen, param_register, param_point, scale_x, scale_y;
  unsigned polygon_mask, line_mask, native_2x, precise_interpolation;
  unsigned front_scale_word, back_scale_word, front_offset_word, back_offset_word;
  unsigned count_coverage, counter_word, counter_stride_bytes;
} XeDxilGuestPixelOptions;
#ifdef __cplusplus
extern "C" {
#endif
/* Host render targets or ROV; textures use the built-in native policy stage.
 * Memory exports use the program's native format/store policy.
 * Export 61.x is guest depth; exports 0..3 are guest colors. */
XeDxilShaderBinary *xe_dxil_build_guest_pixel_owned(const XeDxilGuestPixelOptions *options,
  const uint32_t *words, size_t word_count, unsigned control_flow_count);
#ifdef __cplusplus
}
#endif
#endif
