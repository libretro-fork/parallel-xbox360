/* Typed lifetime/view adapter only; allocation/ownership are MSVC C89. */
#ifndef XENIA_GPU_SHADER_UCODE_H_
#define XENIA_GPU_SHADER_UCODE_H_
#include "libretro/core_shader_ucode.h"
#include <span>
namespace xe {
namespace gpu {
class ShaderUcode final {
 public:
  ShaderUcode() = default;
  ~ShaderUcode() { xe_shader_ucode_close(&storage_); }
  ShaderUcode(const ShaderUcode&) = delete;
  ShaderUcode& operator=(const ShaderUcode&) = delete;
  bool Allocate(size_t count) { return xe_shader_ucode_allocate(&storage_, count) != 0; }
  uint32_t* data() { return storage_.words; }
  const uint32_t* data() const { return storage_.words; }
  size_t size() const { return storage_.count; }
  std::span<const uint32_t> view() const { return {data(), size()}; }
 private:
  xe_shader_ucode storage_ = {};
};
}  // namespace gpu
}  // namespace xe
#endif
