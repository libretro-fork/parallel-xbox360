/* Native bitfield/object lifetime glue for the C89 ownership fixture. */
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/dxil/guest_texture_stage.h"
extern "C" void* xe_binding_native_new() {
  return new xe::gpu::SpirvShader(xe::gpu::xenos::ShaderType::kVertex, 0, nullptr, 0);
}
extern "C" void xe_binding_native_free(void* owner) {
  delete static_cast<xe::gpu::SpirvShader*>(owner);
}
extern "C" int xe_binding_native_publish(void* owner, const XeDxilTextureLayout* layout) {
  auto* shader = static_cast<xe::gpu::SpirvShader*>(owner);
  return shader->PublishNativeBindings(*layout);
}
extern "C" int xe_binding_native_check(void* owner, const XeDxilTextureLayout* layout,
                                      const void** textures, const void** samplers) {
  const auto* shader = static_cast<const xe::gpu::SpirvShader*>(owner);
  if (!shader->bindings_ready()) return 0;
  const auto t = shader->GetTextureBindingsAfterTranslation();
  const auto s = shader->GetSamplerBindingsAfterTranslation();
  if (t.size() != layout->texture_count || s.size() != layout->sampler_count ||
      shader->GetUsedTextureMaskAfterTranslation() != layout->used_texture_mask) return -1;
  for (size_t i = 0; i < t.size(); ++i) {
    const auto& a = t[i]; const auto& b = layout->textures[i];
    if (a.fetch_constant != b.fetch_constant || unsigned(a.dimension) != b.dimension || a.is_signed != b.is_signed) return -1;
  }
  for (size_t i = 0; i < s.size(); ++i) {
    const auto& a = s[i]; const auto& b = layout->samplers[i];
    if (a.fetch_constant != b.fetch_constant || unsigned(a.mag_filter) != b.mag_filter ||
        unsigned(a.min_filter) != b.min_filter || unsigned(a.mip_filter) != b.mip_filter ||
        unsigned(a.aniso_filter) != b.aniso_filter) return -1;
  }
  if (textures) *textures = t.data();
  if (samplers) *samplers = s.data();
  return 1;
}
