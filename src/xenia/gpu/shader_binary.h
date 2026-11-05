/* Typed lifetime adapter only; storage and ownership transfer are C89. */
#ifndef XENIA_GPU_SHADER_BINARY_H_
#define XENIA_GPU_SHADER_BINARY_H_
#include "libretro/core_spv_binary.h"
#include <span>
namespace xe {
namespace gpu {
class ShaderBinary final {
 public:
  ShaderBinary() = default;
  ~ShaderBinary() { xe_spv_binary_close(&storage_); }
  ShaderBinary(const ShaderBinary&) = delete;
  ShaderBinary& operator=(const ShaderBinary&) = delete;
  ShaderBinary(ShaderBinary&& source) noexcept {
    xe_spv_binary_take(&storage_, &source.storage_);
  }
  ShaderBinary& operator=(ShaderBinary&& source) noexcept {
    xe_spv_binary_take(&storage_, &source.storage_);
    return *this;
  }
  xe_spv_binary* storage() { return &storage_; }
  const uint8_t* data() const {
    return reinterpret_cast<const uint8_t*>(storage_.words);
  }
  size_t size() const { return storage_.count * sizeof(uint32_t); }
  std::span<const uint8_t> view() const { return {data(), size()}; }
 private:
  xe_spv_binary storage_ = {};
};
}  // namespace gpu
}  // namespace xe
#endif
