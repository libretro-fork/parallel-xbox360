/* Native guest vertex stage. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_VERTEX_STAGE_H
#define XE_GPU_DXIL_GUEST_VERTEX_STAGE_H
#include "guest_program.h"
#include "guest_texture_stage.h"
#include "shader_module.h"
typedef struct XeDxilGuestVertexOptions {
  XeDxilGuestProgramOptions program;
  const XeDxilTextureStageOptions *textures;
  unsigned system_constants_bytes, flags_word, index_endian_word, index_base_word;
  unsigned ndc_scale_word, ndc_offset_word, clip_planes_word;
  unsigned interpolator_mask, clip_planes, cull_planes, point_size, vertex_kill, vertex_kill_and;
  unsigned w_not_reciprocal_mask, xy_divided_mask, z_divided_mask;
} XeDxilGuestVertexOptions;
#ifdef __cplusplus
extern "C" {
#endif
/* Plain host vertex stage. The input is the already fetched SV_VertexID;
 * index-buffer endian conversion and the guest base index are applied here.
 * Host domain and expanded-strip vertex stages use different input adapters. */
int xe_dxil_build_guest_vertex(const XeDxilGuestVertexOptions *options,
  const uint32_t *words, size_t word_count, unsigned control_flow_count, XeDxilShaderBinary *binary);
XeDxilShaderBinary *xe_dxil_build_guest_vertex_owned(const XeDxilGuestVertexOptions *options,
  const uint32_t *words, size_t word_count, unsigned control_flow_count);
#ifdef __cplusplus
}
#endif
#endif
