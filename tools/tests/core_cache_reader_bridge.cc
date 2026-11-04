/* Minimal C++ bridge to exercise the production templated cache reader. */
#include "xenia/gpu/shader_storage.h"
extern "C" uint64_t xe_test_shader_entries(RFILE* file, unsigned* entries) {
  *entries = 0;
  return xe::gpu::ReadShaderEntries(file,
      [entries](xe::gpu::xenos::ShaderType, const uint32_t*, uint32_t, uint64_t) {
        ++*entries;
        return true;
      });
}
