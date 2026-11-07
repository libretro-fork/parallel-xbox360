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
SpirvShader::~SpirvShader() { xe_binding_storage_reset(&binding_storage_); }

bool SpirvShader::AllocateBindings(size_t textures, size_t samplers) {
  static_assert(sizeof(TextureBinding) == sizeof(uint32_t));
  static_assert(sizeof(SamplerBinding) == sizeof(uint32_t));
  static_assert(alignof(TextureBinding) <= alignof(uint32_t));
  static_assert(alignof(SamplerBinding) <= alignof(uint32_t));
  static_assert(std::is_trivial_v<TextureBinding>);
  static_assert(std::is_trivial_v<SamplerBinding>);
  if (!xe_binding_storage_allocate(&binding_storage_, textures, samplers)) return false;
  // Start array lifetimes without rewriting calloc's initialized representation.
  if (textures) new (binding_storage_.data) TextureBinding[textures];
  if (samplers) new (MutableSamplerBindings()) SamplerBinding[samplers];
  return true;
}

bool SpirvShader::PublishNativeBindings(const XeDxilTextureLayout& layout) {
  if (layout.texture_count > XE_DXIL_TEXTURE_BINDINGS_MAX ||
      layout.sampler_count > XE_DXIL_SAMPLER_BINDINGS_MAX) return false;
  if (bindings_ready()) {
    if (layout.texture_count != binding_storage_.texture_count ||
        layout.sampler_count != binding_storage_.sampler_count ||
        layout.used_texture_mask != used_texture_mask_) return false;
    for (size_t i = 0; i < layout.texture_count; ++i) {
      const auto& a = layout.textures[i]; const auto& b = MutableTextureBindings()[i];
      if (a.fetch_constant != b.fetch_constant || a.dimension != unsigned(b.dimension) || a.is_signed != b.is_signed) return false;
    }
    for (size_t i = 0; i < layout.sampler_count; ++i) {
      const auto& a = layout.samplers[i]; const auto& b = MutableSamplerBindings()[i];
      if (a.fetch_constant != b.fetch_constant || a.mag_filter != unsigned(b.mag_filter) || a.min_filter != unsigned(b.min_filter) ||
          a.mip_filter != unsigned(b.mip_filter) || a.aniso_filter != unsigned(b.aniso_filter)) return false;
    }
    return true;
  }
  if (retro_atomic_load_acquire_int(&bindings_setup_entered_)) return false;
  if (!AllocateBindings(layout.texture_count, layout.sampler_count)) return false;
  for (size_t i = 0; i < layout.texture_count; ++i) {
    const auto& in = layout.textures[i]; auto& out = MutableTextureBindings()[i];
    out.fetch_constant = in.fetch_constant;
    out.dimension = xenos::FetchOpDimension(in.dimension); out.is_signed = in.is_signed;
  }
  for (size_t i = 0; i < layout.sampler_count; ++i) {
    const auto& in = layout.samplers[i]; auto& out = MutableSamplerBindings()[i];
    out.fetch_constant = in.fetch_constant;
    out.mag_filter = xenos::TextureFilter(in.mag_filter); out.min_filter = xenos::TextureFilter(in.min_filter);
    out.mip_filter = xenos::TextureFilter(in.mip_filter); out.aniso_filter = xenos::AnisoFilter(in.aniso_filter);
  }
  used_texture_mask_ = layout.used_texture_mask;
  retro_atomic_store_release_int(&bindings_setup_entered_, 1);
  retro_atomic_store_release_int(&bindings_ready_, 1);
  return true;
}

}  // namespace gpu
}  // namespace xe
