/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader.h"

#include <cstring>
#include <new>
#include <type_traits>
#include "xenia/gpu/dxil/guest_texture_stage.h"

namespace xe {
namespace gpu {

SpirvShader::SpirvShader(xenos::ShaderType shader_type,
                         uint64_t ucode_data_hash, const uint32_t* ucode_dwords,
                         size_t ucode_dword_count,
                         std::endian ucode_source_endian)
    : Shader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
             ucode_source_endian) {}

Shader::Translation* SpirvShader::CreateTranslationInstance(
    uint64_t modification) {
  return new SpirvTranslation(*this, modification);
}
SpirvShader::~SpirvShader() { xe_binding_snapshot_clear(&bindings_); }

xe_binding_snapshot* SpirvShader::AllocateBindings(size_t textures, size_t samplers) {
  static_assert(sizeof(TextureBinding) == sizeof(uint32_t));
  static_assert(sizeof(SamplerBinding) == sizeof(uint32_t));
  static_assert(alignof(TextureBinding) <= alignof(xe_binding_snapshot));
  static_assert(alignof(SamplerBinding) <= alignof(xe_binding_snapshot));
  static_assert(std::is_trivial_v<TextureBinding>);
  static_assert(std::is_trivial_v<SamplerBinding>);
  auto* owner = xe_binding_snapshot_new(textures, samplers);
  if (!owner) return nullptr;
  auto* records = static_cast<uint32_t*>(xe_binding_snapshot_records(owner));
  // Begin native bitfield array lifetimes in the final zero-initialized storage.
  if (textures) new (records) TextureBinding[textures];
  if (samplers) new (reinterpret_cast<unsigned char*>(records) + textures * sizeof(uint32_t)) SamplerBinding[samplers];
  return owner;
}

bool SpirvShader::PublishNativeBindings(const XeDxilTextureLayout& layout) {
  if (layout.texture_count > XE_DXIL_TEXTURE_BINDINGS_MAX ||
      layout.sampler_count > XE_DXIL_SAMPLER_BINDINGS_MAX) return false;
  if (bindings_ready()) {
    const auto textures = GetTextureBindingsAfterTranslation();
    const auto samplers = GetSamplerBindingsAfterTranslation();
    if (layout.texture_count != textures.size() ||
        layout.sampler_count != samplers.size() ||
        layout.used_texture_mask != GetUsedTextureMaskAfterTranslation()) return false;
    for (size_t i = 0; i < layout.texture_count; ++i) {
      const auto& a = layout.textures[i]; const auto& b = textures[i];
      if (a.fetch_constant != b.fetch_constant || a.dimension != unsigned(b.dimension) || a.is_signed != b.is_signed) return false;
    }
    for (size_t i = 0; i < layout.sampler_count; ++i) {
      const auto& a = layout.samplers[i]; const auto& b = samplers[i];
      if (a.fetch_constant != b.fetch_constant || a.mag_filter != unsigned(b.mag_filter) || a.min_filter != unsigned(b.min_filter) ||
          a.mip_filter != unsigned(b.mip_filter) || a.aniso_filter != unsigned(b.aniso_filter)) return false;
    }
    return true;
  }
  auto* owner = AllocateBindings(layout.texture_count, layout.sampler_count);
  if (!owner) return false;
  auto* textures = reinterpret_cast<TextureBinding*>(owner + 1);
  auto* samplers = reinterpret_cast<SamplerBinding*>(
      reinterpret_cast<unsigned char*>(owner + 1) + owner->texture_count * sizeof(uint32_t));
  for (size_t i = 0; i < layout.texture_count; ++i) {
    const auto& in = layout.textures[i]; auto& out = textures[i];
    out.fetch_constant = in.fetch_constant;
    out.dimension = xenos::FetchOpDimension(in.dimension); out.is_signed = in.is_signed;
  }
  for (size_t i = 0; i < layout.sampler_count; ++i) {
    const auto& in = layout.samplers[i]; auto& out = samplers[i];
    out.fetch_constant = in.fetch_constant;
    out.mag_filter = xenos::TextureFilter(in.mag_filter); out.min_filter = xenos::TextureFilter(in.min_filter);
    out.mip_filter = xenos::TextureFilter(in.mip_filter); out.aniso_filter = xenos::AnisoFilter(in.aniso_filter);
  }
  owner->used_texture_mask = layout.used_texture_mask;
  return xe_binding_snapshot_publish(&bindings_, owner) != 0;
}

}  // namespace gpu
}  // namespace xe
