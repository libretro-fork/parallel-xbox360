/* Interop fixture: compare production helper modules against a parent checkout.
 * The legacy vector API is confined to the reference executable. */
#include "xenia/gpu/edram_dump_shader.h"
#include "xenia/gpu/edram_transfer_shader.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef XE_SPV_ARENA_TEST
static xe_spv_arena instruction_arena;
static size_t cold_pages, largest_instruction_count;
#define INSTRUCTION_ARENA_ARGUMENT , &instruction_arena
#else
#define INSTRUCTION_ARENA_ARGUMENT
#endif
#ifdef XE_SPV_OUTPUT_ALLOCATIONS
#include "core_pipeline_bench_profile.h"
#include "xenia/gpu/spirv/spvIR.h"
#include <new>
void* operator new(std::size_t n) { void* p = xe_bench_malloc(n); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { xe_bench_free(p); }
void operator delete[](void* p) noexcept { xe_bench_free(p); }
void operator delete(void* p, std::size_t) noexcept { xe_bench_free(p); }
void operator delete[](void* p, std::size_t) noexcept { xe_bench_free(p); }
#endif
using namespace xe::gpu;
static unsigned modules;
static size_t largest;
static FILE* output;
#ifndef XE_SPV_OUTPUT_LEGACY
static xe_spv_binary storage;
#endif
static void record(const uint32_t* words, size_t count) {
#ifdef XE_SPV_ARENA_TEST
  if (instruction_arena.in_use || (instruction_arena.reserved_records / XE_SPV_RECORDS_PER_PAGE) != cold_pages) std::abort();
  if (xe_spv_arena_used(&instruction_arena) > largest_instruction_count)
    largest_instruction_count = xe_spv_arena_used(&instruction_arena);
#endif
  uint32_t length = uint32_t(count);
  if (count < 5 || words[0] != 0x07230203 ||
      fwrite(&length, sizeof(length), 1, output) != 1 ||
      fwrite(words, sizeof(*words), count, output) != count) std::abort();
  if (count > largest) largest = count;
  ++modules;
}
int main(int argc, char** argv) {
#ifdef XE_SPV_OUTPUT_ALLOCATIONS
  size_t before = xe_bench_allocations();
  size_t bytes_before = xe_bench_requested();
  printf("IR record bytes: Instruction=%zu Block=%zu Function=%zu Module=%zu\n",
         sizeof(spv::Instruction), sizeof(spv::Block), sizeof(spv::Function), sizeof(spv::Module));
#endif
  const unsigned formats[] = {0, 1, 2, 3, 4, 5, 6, 7, 10, 12, 14, 15, 0, 1};
  unsigned pass, f, msaa, bits, mode;
  if ((argc != 2 && argc != 3) || !(output = fopen(argv[1], "wb"))) return 1;
  const bool wide_scale = argc == 3;
#ifdef XE_SPV_ARENA_TEST
  if (!xe_spv_arena_reserve(&instruction_arena, 2048)) return 8;
  cold_pages = (instruction_arena.reserved_records / XE_SPV_RECORDS_PER_PAGE);
#endif
#ifndef XE_SPV_OUTPUT_LEGACY
  if (!xe_spv_binary_reserve(&storage, 16384)) return 2;
  uint32_t* stable = nullptr;
  size_t capacity = 0;
#endif
  for (pass = 0; pass < 2; ++pass) {
    for (f = 0; f < 14; ++f) for (msaa = 0; msaa < 3; ++msaa) for (bits = 0; bits < 16; ++bits) {
      EdramDumpShaderKey key;
      EdramDumpShaderOptions options;
      key.resource_format = formats[f]; key.is_depth = f >= 12;
      key.msaa_samples = xe::gpu::xenos::MsaaSamples(msaa);
      key.direct_resolve = bits & 1; key.native_layout = (bits >> 1) & 1;
      key.source_scale_native = (bits >> 2) & 1;
      options.resolution_scale_x = (bits & 8) ? (wide_scale ? 32 : 3) : 1;
      options.resolution_scale_y = (bits & 8) ? (wide_scale ? 16 : 2) : 1;
      options.msaa_2x_attachments_supported = (bits & 4) != 0;
      options.source_is_uint = !key.is_depth && f < 4 && (bits & 2);
      options.depth_float24_round = (bits & 2) != 0;
      options.depth_float24_convert_in_pixel_shader = (bits & 4) != 0;
#ifdef XE_SPV_OUTPUT_LEGACY
      std::vector<uint32_t> words = BuildEdramDumpShaderSpirv(key, options);
      record(words.data(), words.size());
#else
      if (!BuildEdramDumpShaderSpirv(key, options, storage INSTRUCTION_ARENA_ARGUMENT)) return 3;
      record(storage.words, storage.count);
#endif
    }
    for (mode = 0; mode < unsigned(EdramTransferMode::kCount); ++mode)
      for (f = 0; f < 8; ++f) for (bits = 0; bits < 8; ++bits) {
      EdramTransferShaderKey key;
      EdramTransferShaderOptions options;
      key.mode = EdramTransferMode(mode);
      key.source_resource_format = EdramTransferSourceIsColor(key.mode) ? formats[f] : (f & 1);
      key.dest_resource_format = kEdramTransferModes[mode].output == EdramTransferOutput::kColor ? formats[7-f] : (f & 1);
      key.source_msaa_samples = xe::gpu::xenos::MsaaSamples(f % 3);
      key.dest_msaa_samples = xe::gpu::xenos::MsaaSamples(bits % 3);
      key.host_depth_source_msaa_samples = EdramTransferHostDepthIsCopy(key.mode) ? xe::gpu::xenos::MsaaSamples::k1X : key.dest_msaa_samples;
      key.source_scale_native = bits & 1; key.dest_scale_native = (bits >> 1) & 1;
      options.resolution_scale_x = (bits & 4) ? 3 : 1;
      options.resolution_scale_y = (bits & 4) ? 2 : 1;
      options.msaa_2x_attachments_supported = (bits & 2) != 0;
      options.source_color_is_uint = EdramTransferSourceIsColor(key.mode) && f < 4 && (bits & 1);
      options.stencil_reference_output_supported = (bits & 1) != 0;
      options.sample_rate_shading_supported = (bits & 2) != 0;
      options.depth_float24_round = (bits & 1) != 0;
      options.depth_float24_convert_in_pixel_shader = (bits & 2) != 0;
      options.no_discard_stencil = (bits & 4) != 0;
      options.fast_pitch_divmod = (bits & 4) != 0;
#ifdef XE_SPV_OUTPUT_LEGACY
      std::vector<uint32_t> words = BuildEdramTransferShaderSpirv(key, options);
      record(words.data(), words.size());
#else
      if (!BuildEdramTransferShaderSpirv(key, options, storage INSTRUCTION_ARENA_ARGUMENT)) return 4;
      record(storage.words, storage.count);
#endif
    }
#ifndef XE_SPV_OUTPUT_LEGACY
    if (!pass) { stable = storage.words; capacity = storage.capacity; }
    else if (storage.words != stable || storage.capacity != capacity) return 5;
#endif
  }
#ifndef XE_SPV_OUTPUT_LEGACY
  xe_spv_binary_close(&storage);
#endif
#ifdef XE_SPV_ARENA_TEST
  printf("Arena: %zu cold pages, %zu peak records, zero page allocations during generation\n",
         cold_pages, largest_instruction_count);
  if (!xe_spv_arena_close(&instruction_arena)) return 9;
#endif
  if (fclose(output)) return 6;
  printf("PASS: %u helper modules (%s), largest %zu words\n",
         modules, wide_scale ? "wide scale" : "standard scale", largest);
#ifdef XE_SPV_OUTPUT_ALLOCATIONS
  printf("Tracked C++ new + arena-page + ID-index allocations across %u modules: %zu; live bytes: %zu\n",
         modules, xe_bench_allocations() - before, xe_bench_live());
  printf("Tracked C++ + arena + ID-index requested bytes: %zu; peak tracked live bytes: %zu\n",
         xe_bench_requested() - bytes_before, xe_bench_peak());
#endif
  return 0;
}
