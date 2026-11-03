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
#include <utility>

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
bool SpirvShader::PublishNativeBindings(std::vector<TextureBinding>&& textures,
                                        std::vector<SamplerBinding>&& samplers,
                                        uint32_t used_texture_mask) {
  if (bindings_ready()) {
    if (textures.size() != texture_bindings_.size() || samplers.size() != sampler_bindings_.size() ||
        used_texture_mask != used_texture_mask_) return false;
    for (size_t i = 0; i < textures.size(); ++i) {
      const auto& a = textures[i]; const auto& b = texture_bindings_[i];
      if (a.fetch_constant != b.fetch_constant || a.dimension != b.dimension || a.is_signed != b.is_signed) return false;
    }
    for (size_t i = 0; i < samplers.size(); ++i) {
      const auto& a = samplers[i]; const auto& b = sampler_bindings_[i];
      if (a.fetch_constant != b.fetch_constant || a.mag_filter != b.mag_filter || a.min_filter != b.min_filter ||
          a.mip_filter != b.mip_filter || a.aniso_filter != b.aniso_filter) return false;
    }
    return true;
  }
  if (bindings_setup_entered_.test_and_set(std::memory_order_relaxed)) return false;
  texture_bindings_ = std::move(textures); sampler_bindings_ = std::move(samplers);
  used_texture_mask_ = used_texture_mask;
  bindings_ready_.store(true,std::memory_order_release);
  return true;
}

}  // namespace gpu
}  // namespace xe
