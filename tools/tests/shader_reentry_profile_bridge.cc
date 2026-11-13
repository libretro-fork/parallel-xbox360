/* Test-only typed allocation and Shader lifetime interoperability. All fixture
 * generation, counters, comparisons and reports are C89. No release interpose. */
#include "xenia/gpu/spirv_shader.h"
#include "core_pipeline_bench_profile.h"
#include <new>
void* operator new(size_t bytes) {
  if (void* p = xe_bench_malloc(bytes ? bytes : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { xe_bench_free(p); }
void operator delete[](void* p) noexcept { xe_bench_free(p); }
void operator delete(void* p, size_t) noexcept { xe_bench_free(p); }
void operator delete[](void* p, size_t) noexcept { xe_bench_free(p); }
extern "C" int xe_reentry_profile(const uint32_t* code, size_t words,
    unsigned pixel, uint64_t* masks, size_t capacity, size_t* statistics) {
  using namespace xe::gpu;
  const size_t before = xe_bench_live();
  {
    SpirvShader shader(pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex,
                       0, code, words, std::endian::native);
    const size_t calls = xe_bench_allocations(), bytes = xe_bench_requested();
    shader.AnalyzeUcode(static_cast<xe::StringBuffer*>(nullptr));
    statistics[0] = xe_bench_allocations() - calls;
    statistics[1] = xe_bench_requested() - bytes;
    statistics[2] = shader.cf_pair_index_bound() * 2;
    statistics[3] = shader.label_addresses().size();
    if (statistics[2] > capacity) return 0;
    for (size_t i = 0; i < statistics[2]; ++i)
      masks[i] = shader.GetRegisterComponentsWrittenBeforeReentering(uint32_t(i));
  }
  return xe_bench_live() == before;
}
