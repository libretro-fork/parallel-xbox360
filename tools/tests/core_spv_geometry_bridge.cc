/* Typed interop only; both revisions compile their own complete builder. */
#include "xenia/gpu/spirv_builtin_geometry_shader.h"
#include "xenia/gpu/spirv/SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef XE_SPV_GEOMETRY_PROFILE
#include "core_pipeline_bench_profile.h"
#include <new>
void* operator new(size_t n) { if (void* p = xe_bench_malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { xe_bench_free(p); }
void operator delete[](void* p) noexcept { xe_bench_free(p); }
void operator delete(void* p, size_t) noexcept { xe_bench_free(p); }
void operator delete[](void* p, size_t) noexcept { xe_bench_free(p); }
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
struct request { unsigned type, interpolators, planes, flags, controls, version; };
static xe_spv_binary binary;
static xe_spv_arena arena;
static size_t largest, peak_records;
static unsigned modules;
static const char* validation_directory;
static void record(FILE* file, const uint32_t* words, size_t count, const request& p) {
  CHECK(count >= 5 && words[0] == spv::MagicNumber);
  if (count > largest) largest = count;
  ++modules;
  if (!file) return;
  uint32_t length = uint32_t(count), entry_function = 0, function = 0;
  size_t interfaces = 0;
  for (size_t i = 5; i < count;) {
    unsigned size = words[i] >> 16, opcode = words[i] & 65535;
    CHECK(size && size <= count - i);
    if (opcode == unsigned(spv::Op::OpEntryPoint)) {
      CHECK(size >= 5 && !entry_function);
      entry_function = words[i + 2]; interfaces = size - 5;
    }
    if (opcode == unsigned(spv::Op::OpFunction) && !function) function = words[i + 2];
    i += size;
  }
  CHECK(entry_function && entry_function == function);
  CHECK(interfaces == p.interpolators * 2 + 2 + !!(p.flags & 4) + !!(p.flags & 8) +
      ((p.type == 1 || p.type == 4) && p.version >= spv::Spv_1_4));
  CHECK(std::fwrite(&length, sizeof(length), 1, file) == 1);
  CHECK(std::fwrite(words, sizeof(*words), count, file) == count);
  if (validation_directory && p.interpolators == 16 && p.planes == 6 && p.flags == 15) {
    char path[1024];
    CHECK(std::snprintf(path, sizeof(path), "%s/geometry-%u-%u-%u.spv", validation_directory,
        p.type, p.version, p.controls) > 0);
    FILE* module = std::fopen(path, "wb");
    CHECK(module && std::fwrite(words, sizeof(*words), count, module) == count);
    CHECK(!std::fclose(module));
  }
}
static void emit(FILE* file, const request& p) {
#ifdef XE_SPV_GEOMETRY_LEGACY
  auto code = xe::gpu::BuildGuestPrimitiveGeometryShaderSpirv(
      xe::gpu::BuiltinGeometryShaderType(p.type), p.interpolators, p.planes,
      p.flags & 1, p.flags & 2, p.flags & 4, p.flags & 8, p.version,
      p.controls & 1, p.controls & 2, p.controls & 4);
  record(file, code.data(), code.size(), p);
#else
  CHECK(xe::gpu::BuildGuestPrimitiveGeometryShaderSpirv(
      xe::gpu::BuiltinGeometryShaderType(p.type), p.interpolators, p.planes,
      p.flags & 1, p.flags & 2, p.flags & 4, p.flags & 8, p.version,
      p.controls & 1, p.controls & 2, p.controls & 4, binary, &arena));
  CHECK(!arena.in_use);
  if (xe_spv_arena_used(&arena) > peak_records) peak_records = xe_spv_arena_used(&arena);
  record(file, binary.words, binary.count, p);
#endif
}
static void batch(FILE* file, bool bench) {
  static const unsigned interpolators[] = {0, 8, 16};
  static const unsigned planes[] = {0, 1, 6};
  static const unsigned versions[] = {spv::Spv_1_0, spv::Spv_1_3, spv::Spv_1_4, spv::Spv_1_6};
  request p;
  for (unsigned v = 0; v < (bench ? 1u : 4u); ++v)
    for (p.type = 1; p.type <= 4; ++p.type)
      for (unsigned n = 0; n < (bench ? 2u : 3u); ++n)
        for (unsigned c = 0; c < (bench ? 2u : 3u); ++c)
          for (unsigned f = 0; f < (bench ? 2u : 16u); ++f)
            for (unsigned controls = 0; controls < (bench ? 2u : 8u); ++controls) {
              p.interpolators = interpolators[bench ? n * 2 : n];
              p.planes = planes[bench ? c * 2 : c];
              p.flags = bench ? f * 15 : f;
              p.controls = bench ? controls * 7 : controls;
              p.version = bench ? spv::Spv_1_6 : versions[v];
              emit(file, p);
            }
}
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 1;
  const bool bench = argc == 3 && !std::strcmp(argv[2], "bench");
  validation_directory = bench ? nullptr : std::getenv("SPV_GEOMETRY_VALIDATION");
#ifndef XE_SPV_GEOMETRY_LEGACY
  CHECK(xe_spv_binary_reserve(&binary, 4096));
  CHECK(xe_spv_arena_reserve(&arena, 1024));
  CHECK(xe_spv_arena_reserve_objects(&arena, sizeof(spv::Block), 64, sizeof(spv::Function), 32));
#endif
  FILE* file = std::fopen(argv[1], "wb");
  CHECK(file);
  batch(file, bench);
  CHECK(modules == (bench ? 64u : 18432u));
  CHECK(!std::fclose(file));
#ifdef XE_SPV_GEOMETRY_PROFILE
  if (bench) {
    const size_t warmed = xe_bench_live(), calls = xe_bench_allocations(), bytes = xe_bench_requested();
    const uint64_t start = xe_bench_time();
    for (unsigned r = 0; r < 20; ++r) batch(nullptr, true);
    const uint64_t elapsed = xe_bench_time() - start;
    CHECK(xe_bench_live() == warmed);
    std::printf("geometry modules=1280 calls=%zu requested=%zu retained=%zu peak=%zu ms=%.3f\n",
        xe_bench_allocations() - calls, xe_bench_requested() - bytes, warmed,
        xe_bench_peak(), double(elapsed) / 1000.0);
  }
#endif
#ifndef XE_SPV_GEOMETRY_LEGACY
  CHECK(binary.capacity == 4096 && arena.reserved_records == 1024);
  uint32_t* const retained = binary.words;
  for (unsigned invalid = 0; invalid < 3; ++invalid) {
    CHECK(!xe::gpu::BuildGuestPrimitiveGeometryShaderSpirv(
        xe::gpu::BuiltinGeometryShaderType(invalid ? 1 : 0), invalid == 1 ? 17 : 0,
        invalid == 2 ? 7 : 0, false, false, false, false, spv::Spv_1_6,
        false, false, false, binary, &arena));
    CHECK(!binary.count && binary.words == retained && !arena.in_use);
  }
  xe_spv_binary_close(&binary);
  CHECK(xe_spv_arena_close(&arena));
#endif
#ifdef XE_SPV_GEOMETRY_PROFILE
  CHECK(!xe_bench_live());
#endif
  std::printf("PASS: geometry modules=%u largest_words=%zu peak_instruction_records=%zu\n", modules, largest, peak_records);
  return 0;
}
