/* Actual LZX/XexModule interop; fixtures and checks are C89. No renderer. */
#include "xenia/cpu/lzx.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/cpu/processor.h"
#include <cstring>
extern "C" void xe_test_prepare_delta(int, int, const uint8_t**, size_t*,
    const uint8_t**, size_t*, const uint8_t**);
struct mspack_file;
extern mspack_memory_file_t* mspack_memory_open(mspack_system*, void*, size_t);
extern void mspack_memory_close(mspack_memory_file_t*);
extern int mspack_memory_read(mspack_file*, void*, int);
extern int mspack_memory_write(mspack_file*, void*, int);
extern "C" int xe_test_delta_frame(const uint8_t* data, size_t size, uint8_t* out, size_t length) {
  LzxFrameDecoder decoder(32768);
  return decoder.Reset() && decoder.Decompress(data, size, out, length) &&
      decoder.Reset() && decoder.Decompress(data, size, out, length);
}
extern "C" int xe_test_lzx_adapter(void) {
  uint8_t data[8] = {}, out[8] = {};
  LzxFrameDecoder invalid(3), empty(0), decoder(32768);
  if (invalid.Reset() || empty.Reset() || !decoder.Reset() ||
      decoder.Decompress(nullptr, 8, out, 8) ||
      decoder.Decompress(data, (size_t)0x80000000U, out, 8) ||
      decoder.Decompress(data, 8, nullptr, 8)) return 0;
  auto* file = mspack_memory_open(nullptr, data, sizeof(data));
  if (!file) return 0;
  auto* opaque = reinterpret_cast<mspack_file*>(file);
  const bool valid = mspack_memory_read(opaque, out, -1) == -1 &&
      mspack_memory_write(opaque, out, -1) == -1 &&
      mspack_memory_read(opaque, nullptr, 0) == 0 &&
      mspack_memory_read(opaque, out, 16) == 8 &&
      mspack_memory_read(opaque, out, 1) == 0;
  mspack_memory_close(file);
  return valid && lzx_decompress(data, 8, out, 8, 4, data, 8) != 0 &&
      lzx_decompress(nullptr, 0, out, 8, 32768, data, 8) != 0 &&
      lzx_decompress(nullptr, 0, nullptr, 0, 0, nullptr, 0) == 0;
}
extern "C" int xe_test_delta_decoder(const uint8_t* patch, size_t size,
    uint8_t* output, size_t capacity, size_t reference) {
  return lzxdelta_apply_patch(patch, size, 32768, output, capacity, reference);
}
extern "C" int xe_test_delta_module(int encrypted, int corrupted) {
  const uint8_t *base, *patch, *expected;
  size_t base_length, patch_length;
  xe_test_prepare_delta(encrypted, corrupted, &base, &base_length,
      &patch, &patch_length, &expected);
  xe::Memory memory;
  if (!memory.Initialize()) return 0;
  xe::cpu::Processor processor(&memory, nullptr);
  xe::cpu::XexModule module(&processor, nullptr), delta(&processor, nullptr);
  if (!module.Load("base", "base", base, base_length) ||
      !delta.Load("delta", "delta", patch, patch_length)) return 0;
  const int result = delta.ApplyPatch(&module);
  if (corrupted ? result == 0 : result != 0) return 0;
  if (std::memcmp(memory.TranslateVirtual(0x82000000), expected, 65536)) return 0;
  return delta.Unload() && module.Unload();
}
