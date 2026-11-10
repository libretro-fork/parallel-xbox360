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
#include "libretro/core_binding_snapshot.h"

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
    const auto* owner = xe_binding_snapshot_get(&bindings_);
    if (!owner || !owner->texture_count) return {};
    return {reinterpret_cast<const TextureBinding*>(owner + 1), owner->texture_count};
  }
  const uint32_t GetUsedTextureMaskAfterTranslation() const {
    const auto* owner = xe_binding_snapshot_get(&bindings_);
    return owner ? owner->used_texture_mask : 0;
  }

  struct SamplerBinding {
    uint32_t fetch_constant : 5;
    xenos::TextureFilter mag_filter : 2;
    xenos::TextureFilter min_filter : 2;
    xenos::TextureFilter mip_filter : 2;
    xenos::AnisoFilter aniso_filter : 3;
  };
  std::span<const SamplerBinding> GetSamplerBindingsAfterTranslation() const {
    const auto* owner = xe_binding_snapshot_get(&bindings_);
    if (!owner || !owner->sampler_count) return {};
    return {reinterpret_cast<const SamplerBinding*>(
                reinterpret_cast<const unsigned char*>(owner + 1) + owner->texture_count * sizeof(uint32_t)),
            owner->sampler_count};
  }

  // Acquires a complete immutable owner; no partially initialized records are
  // visible. Published records survive every translation until joined teardown.
  bool bindings_ready() const {
    return xe_binding_snapshot_get(&bindings_) != nullptr;
  }
  // Native emission still serializes duplicate binary generation separately;
  // binding publication itself is lock-free and does not require this mutex.
  std::mutex& native_emission_mutex() { return native_emission_mutex_; }
  bool PublishNativeBindings(const XeDxilTextureLayout& layout);

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  friend class SpirvShaderTranslator;

  std::mutex native_emission_mutex_;
  retro_atomic_ptr_t bindings_{};
  static xe_binding_snapshot* AllocateBindings(size_t textures, size_t samplers);

};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_H_
