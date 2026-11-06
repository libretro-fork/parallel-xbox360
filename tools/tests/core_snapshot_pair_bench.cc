// Offline CPU fixture: legacy vector, actual constant type, benchmark-only clock.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "core_snapshot_pair.h"
#include "xenia/gpu/spirv_shader_translator.h"
using Constants = xe::gpu::SpirvShaderTranslator::SystemConstants;
#if defined(_MSC_VER)
#define BOUNDARY __declspec(noinline)
#else
#define BOUNDARY __attribute__((noinline, noipa))
#endif
static BOUNDARY void generate(Constants* s, uint32_t id) {
  std::memset(s, 0, sizeof(*s));
  s->flags = id; s->zpd_fsi_counter_index = UINT32_MAX;
  s->ndc_scale[0] = s->ndc_scale[1] = s->ndc_scale[2] = 1.0f;
  s->vertex_base_index = int32_t(id); s->vertex_index_count = 1024;
  for (uint32_t i = 0; i < 32; ++i) s->texture_integer_scale_bits[i] = id + i;
  for (uint32_t i = 0; i < 4; ++i) s->color_exp_bias[i] = float(id + i);
  s->alpha_test_reference = float(id); s->user_clip_planes[0][0] = float(id);
}
static BOUNDARY uint64_t consume(const Constants* s) {
  return s->flags + uint64_t(s->vertex_index_count) + s->texture_integer_scale_bits[31] + s->zpd_fsi_counter_index;
}
struct Old {
  std::vector<uint8_t> shadow;
  Constants upload;
  bool up = false;
  size_t allocations = 0, shadow_bytes = 0, uploads = 0;
  BOUNDARY uint64_t step(uint32_t id, bool invalidate) {
    Constants value; generate(&value, id);
    if (invalidate) up = false;
    if (shadow.size() != sizeof(value) || std::memcmp(shadow.data(), &value, sizeof(value))) {
      size_t cap = shadow.capacity();
      shadow.assign(reinterpret_cast<const uint8_t*>(&value), reinterpret_cast<const uint8_t*>(&value) + sizeof(value));
      if (shadow.capacity() != cap) ++allocations;
      shadow_bytes += sizeof(value); up = false;
    }
    if (!up) { std::memcpy(&upload, &value, sizeof(value)); ++uploads; up = true; }
    return consume(&upload);
  }
};
struct Pair {
  Constants slots[2], upload;
  xe_snapshot_pair state;
  Pair() { xe_snapshot_pair_init(&state, &slots[0], &slots[1]); }
  bool up = false;
  size_t uploads = 0;
  BOUNDARY uint64_t step(uint32_t id, bool invalidate) {
    Constants* candidate = static_cast<Constants*>(state.candidate); generate(candidate, id);
    if (invalidate) up = false;
    if (xe_snapshot_pair_commit(&state, sizeof(Constants))) up = false;
    if (!up) { std::memcpy(&upload, state.active, sizeof(Constants)); ++uploads; up = true; }
    return consume(&upload);
  }
};
static volatile uint64_t sink;
template<class T> static double measure(T& storage, uint32_t draws, uint32_t period, uint64_t* checksum) {
  uint64_t sum = 0;
  auto begin = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < draws; ++i) sum += storage.step(period ? i / period : 0, i % 257 == 0);
  double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count()/draws;
  sink = sum; *checksum = sum; return ns;
}
static void run(const char* name, uint32_t period) {
  Old old; Pair pair;
  if (old.step(0, false) != pair.step(0, false)) std::abort();
  size_t cold = old.allocations;
  double a[7], b[7]; uint64_t x, y;
  for (unsigned sample = 0; sample < 7; ++sample) {
    if (sample & 1) { b[sample] = measure(pair, 200000, period, &y); a[sample] = measure(old, 200000, period, &x); }
    else { a[sample] = measure(old, 200000, period, &x); b[sample] = measure(pair, 200000, period, &y); }
    if (x != y || old.uploads != pair.uploads || old.allocations != cold) std::abort();
  }
  std::sort(a,a+7); std::sort(b,b+7);
  std::printf("%s: vector %.2f ns/draw, pair %.2f ns/draw (%.2fx); cold shadow allocations %zu/0, warm 0/0, shadow-copy bytes %zu/0, equal CPU upload copies %zu\n", name, a[3], b[3], a[3]/b[3], cold, old.shadow_bytes, old.uploads);
}
int main() {
#if defined(_MSC_VER)
  std::printf("MSVC %d /O2; ", _MSC_VER);
#else
  std::printf("GCC %s -O2; ", __VERSION__);
#endif
  std::printf("actual SystemConstants %zu bytes, 7 interleaved samples x 200,000 draws, median; synthetic generation+dirty check+CPU upload copy; no D3D device\n",sizeof(Constants));
  run("unchanged",0); run("changed-every-draw",1); run("changed-every-eight",8); run("changed-every-64",64);
  std::printf("Requested persistent CPU snapshot payload: old vector %zu, pair %zu + state %zu; old local temporary %zu, pair no local payload\n",sizeof(Constants),sizeof(Constants[2]),sizeof(xe_snapshot_pair),sizeof(Constants));
}
