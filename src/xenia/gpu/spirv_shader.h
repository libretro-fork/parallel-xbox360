/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_H_
#define XENIA_GPU_SPIRV_SHADER_H_

#include <retro_atomic.h>
#include <mutex>
#include <span>
#include "libretro/core_binding_storage.h"

#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

struct XeDxilTextureLayout;

namespace xe {
namespace gpu {

class SpirvShaderTranslator;

class SpirvShader : public Shader {
 public:
  class SpirvTranslation : public Translation {
   public:
    explicit SpirvTranslation(SpirvShader& shader, uint64_t modification)
        : Translation(shader, modification) {}
  };

  explicit SpirvShader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
                       const uint32_t* ucode_dwords, size_t ucode_dword_count,
                       std::endian ucode_source_endian = std::endian::big);

  ~SpirvShader() override;

  // Resource bindings are gathered after the successful translation of any
  // modification for simplicity of translation (and they don't depend on
  // modification bits).

  struct TextureBinding {
    uint32_t fetch_constant : 5;
    // Stacked and 3D are separate TextureBindings.
    xenos::FetchOpDimension dimension : 2;
    uint32_t is_signed : 1;
  };
  // Safe to hash and compare with memcmp for layout hashing.
  std::span<const TextureBinding> GetTextureBindingsAfterTranslation() const {
    if (!binding_storage_.texture_count) return {};
    return {static_cast<const TextureBinding*>(binding_storage_.data),
            binding_storage_.texture_count};
  }
  const uint32_t GetUsedTextureMaskAfterTranslation() const {
    return used_texture_mask_;
  }

  struct SamplerBinding {
    uint32_t fetch_constant : 5;
    xenos::TextureFilter mag_filter : 2;
    xenos::TextureFilter min_filter : 2;
    xenos::TextureFilter mip_filter : 2;
    xenos::AnisoFilter aniso_filter : 3;
  };
  std::span<const SamplerBinding> GetSamplerBindingsAfterTranslation() const {
    if (!binding_storage_.sampler_count) return {};
    return {MutableSamplerBindings(), binding_storage_.sampler_count};
  }

  // True once a translation has gathered the resource bindings above. Set with
  // release ordering after the binding records are filled (which may happen on
  // a pipeline creation thread), so a draw-thread reader that sees this true
  // (acquire) is guaranteed the bindings are fully written and immutable.
  bool bindings_ready() const {
    return retro_atomic_load_acquire_int(&bindings_ready_);
  }
  std::mutex& resource_bindings_mutex() { return resource_bindings_mutex_; }
  // The caller holds resource_bindings_mutex through generation/publication.
  bool PublishNativeBindings(const XeDxilTextureLayout& layout);

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  friend class SpirvShaderTranslator;

  retro_atomic_int_t bindings_setup_entered_{0};
  std::mutex resource_bindings_mutex_;
  retro_atomic_int_t bindings_ready_{0};
  bool AllocateBindings(size_t textures, size_t samplers);
  TextureBinding* MutableTextureBindings() const {
    return static_cast<TextureBinding*>(binding_storage_.data);
  }
  SamplerBinding* MutableSamplerBindings() const {
    return binding_storage_.data ? reinterpret_cast<SamplerBinding*>(
        static_cast<unsigned char*>(binding_storage_.data) +
        binding_storage_.texture_count * sizeof(TextureBinding)) : nullptr;
  }
  xe_binding_storage binding_storage_{};
  uint32_t used_texture_mask_ = 0;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_H_
