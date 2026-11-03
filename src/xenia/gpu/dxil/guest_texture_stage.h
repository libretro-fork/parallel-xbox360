/* Direct guest texture policy and descriptor layout. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_TEXTURE_STAGE_H
#define XE_GPU_DXIL_GUEST_TEXTURE_STAGE_H
#include "guest_texture.h"
#include "shader_module.h"
#define XE_DXIL_TEXTURE_BINDINGS_MAX 192 /* 32 constants, 3 view types, 2 signs. */
#define XE_DXIL_SAMPLER_BINDINGS_MAX 256
typedef struct XeDxilTextureBinding { unsigned fetch_constant, dimension, is_signed; } XeDxilTextureBinding;
typedef struct XeDxilSamplerBinding { unsigned fetch_constant, mag_filter, min_filter, mip_filter, aniso_filter; } XeDxilSamplerBinding;
typedef struct XeDxilTextureLayout {
  unsigned texture_count, sampler_count, used_texture_mask;
  XeDxilTextureBinding textures[XE_DXIL_TEXTURE_BINDINGS_MAX];
  XeDxilSamplerBinding samplers[XE_DXIL_SAMPLER_BINDINGS_MAX];
} XeDxilTextureLayout;
typedef struct XeDxilTextureStageOptions {
  const XeDxilTextureLayout *layout;
  unsigned pixel_shader, system_constants_bytes, signs_word, integer_scale_word;
} XeDxilTextureStageOptions;
typedef struct XeDxilTextureStage XeDxilTextureStage;
#ifdef __cplusplus
extern "C" {
#endif
/* Collect all used instructions before emitting handles. Queries need no
 * descriptors. Samplers follow textures in two-dword index-buffer entries. */
int xe_dxil_texture_layout_add(XeDxilTextureLayout *layout,
  const XeDxilTextureInstruction *instruction, unsigned pixel_shader);
int xe_dxil_texture_layout_texture(const XeDxilTextureLayout *layout,
  unsigned fetch_constant, unsigned dimension, unsigned is_signed);
int xe_dxil_texture_layout_sampler(const XeDxilTextureLayout *layout,
  const XeDxilTextureInstruction *instruction, unsigned pixel_shader);
/* Create in the entry block after enabling SM 6.6, before guest control flow. */
XeDxilTextureStage *xe_dxil_texture_stage_create(XeDxilShaderModule *shader,
  XeDxilValue system, XeDxilValue fetch, const XeDxilTextureStageOptions *options);
void xe_dxil_texture_stage_destroy(XeDxilTextureStage *stage);
int xe_dxil_texture_stage_fetch(XeDxilTextureStage *stage, XeDxilAlu *alu,
  XeDxilAluIo *io, const XeDxilTextureInstruction *instruction,
  const XeDxilTextureOperands *operands, XeDxilValue result[4]);
#ifdef __cplusplus
}
#endif
#endif
