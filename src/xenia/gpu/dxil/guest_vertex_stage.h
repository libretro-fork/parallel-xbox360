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
  unsigned domain, control_points; /* 0 plain vertex, 2 triangle, 3 quad. */
} XeDxilGuestVertexOptions;
#ifdef __cplusplus
extern "C" {
#endif
/* Host type values follow Shader::HostVertexShaderType. Helper asset order:
 * indexed/adaptive VS; discrete tri1/tri3/quad1/quad4 HS; continuous equivalents;
 * adaptive triangle/quad HS. */
int xe_dxil_guest_domain_config(unsigned host_type, unsigned *domain, unsigned *points);
int xe_dxil_guest_tessellation_hosts(unsigned mode, unsigned host_type, unsigned *vs, unsigned *hs);
/* Plain host vertex stage. The input is the already fetched SV_VertexID;
 * index-buffer endian conversion and the guest base index are applied here.
 * Domain mode reads XEVERTEXID control points and hardware domain locations;
 * one control point selects patch-indexed guest registers. */
int xe_dxil_build_guest_vertex(const XeDxilGuestVertexOptions *options,
  const uint32_t *words, size_t word_count, unsigned control_flow_count, XeDxilShaderBinary *binary);
XeDxilShaderBinary *xe_dxil_build_guest_vertex_owned(const XeDxilGuestVertexOptions *options,
  const uint32_t *words, size_t word_count, unsigned control_flow_count);
#ifdef __cplusplus
}
#endif
#endif
