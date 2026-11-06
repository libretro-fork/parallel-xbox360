// Thin actual-layout/owner boundary; state-machine fixture is MSVC C89.
#ifdef XE_SNAPSHOT_OWNER_CHECK
#include <type_traits>
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
using Processor = xe::gpu::d3d12::D3D12CommandProcessor;
static_assert(!std::is_copy_constructible_v<Processor>);
static_assert(!std::is_move_constructible_v<Processor>);
static_assert(!std::is_copy_assignable_v<Processor>);
static_assert(!std::is_move_assignable_v<Processor>);
#else
#include <cstdio>
#include "xenia/gpu/spirv_shader_translator.h"
extern "C" int xe_snapshot_pair_test(size_t size);
int main() {
  using Constants = xe::gpu::SpirvShaderTranslator::SystemConstants;
  static_assert(sizeof(Constants[2]) == sizeof(Constants) * 2);
  std::printf("Actual SystemConstants snapshot stride: %zu bytes; inline slots %zu; alignment %zu\n", sizeof(Constants), sizeof(Constants[2]), alignof(Constants));
  return xe_snapshot_pair_test(sizeof(Constants));
}
#endif
