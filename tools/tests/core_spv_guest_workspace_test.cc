/* Offline real-translator bridge; output can be compared with an older build. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/base/string_buffer.h"
#include <cstdio>
#include <cstdlib>
#include <bit>
#include <cstring>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
/* C89-shaped wire inspection, with unaligned-safe reads and bounded steps. */
static uint32_t read_word(const uint8_t* bytes, size_t index) {
  uint32_t word;
  std::memcpy(&word, bytes + index * sizeof(word), sizeof(word));
  return word;
}
static uint32_t continuation_opcode(const uint8_t* bytes, size_t size) {
  size_t i, count, words = size / sizeof(uint32_t);
  uint32_t header, continuation = 0;
  if (size % sizeof(uint32_t)) return 0;
  for (i = 5; i < words; i += count) {
    header = read_word(bytes, i);
    count = header >> 16;
    if (!count || count > words - i) return 0;
    if ((header & 0xffff) == 246 && count >= 3) { /* OpLoopMerge */
      continuation = read_word(bytes, i + 2);
      break;
    }
  }
  if (!continuation) return 0;
  for (i = 5; i < words; i += count) {
    header = read_word(bytes, i);
    count = header >> 16;
    if (!count || count > words - i) return 0;
    if ((header & 0xffff) == 248 && count == 2 && /* OpLabel */
        read_word(bytes, i + 1) == continuation && words - i > count)
      return read_word(bytes, i + count) & 0xffff;
  }
  return 0;
}
int main() {
  const char* path = std::getenv("SPV_GUEST_OUTPUT");
  CHECK(path);
  FILE* output = std::fopen(path, "wb");
  CHECK(output);
  // Native-order packed 48-bit CF pairs: NOP/NOP; JMP 1/empty EXEC_END;
  // empty EXEC_END/unreached CALL 2. The last case has labels but no incoming
  // continuation edge, exercising the CopyObject fallback rather than OpPhi.
  const uint32_t programs[3][3] = {
      {0, 0, 0},
      {0x2001, 0x0001b000, 0x20000000},
      {1, 0x20022000, 0x90000000}};
  for (unsigned features = 0; features < 2; ++features) {
    xe::gpu::SpirvShaderTranslator translator(
        xe::gpu::SpirvShaderTranslator::Features(features != 0),
        false, false, false, false, 1, 1);
    for (unsigned round = 0; round < 128; ++round) {
      bool pixel = (round & 1) != 0;
      unsigned program = (round / 2) % 3;
      xe::gpu::Shader shader(pixel ? xe::gpu::xenos::ShaderType::kPixel
                                  : xe::gpu::xenos::ShaderType::kVertex,
                            round, programs[program], 3, std::endian::native);
      xe::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly);
      CHECK(shader.label_addresses().empty() == (program == 0));
      uint64_t modification = pixel
          ? translator.GetDefaultPixelShaderModification(4)
          : translator.GetDefaultVertexShaderModification(4);
      auto* translation = shader.GetOrCreateTranslation(modification);
      CHECK(translator.TranslateAnalyzedShader(*translation));
      CHECK(translation->is_valid());
      const auto& words = translation->translated_binary();
      CHECK(continuation_opcode(words.data(), words.size()) ==
            (program == 0 ? 249u : program == 1 ? 245u : 83u));
      uint32_t size = uint32_t(words.size());
      CHECK(size && std::fwrite(&size, sizeof(size), 1, output) == 1);
      CHECK(std::fwrite(words.data(), 1, size, output) == size);
    }
    // The last builder is still live when the translator is destroyed.
  }
  CHECK(std::fclose(output) == 0);
  std::puts("PASS: 256 real guest translations, vertex/pixel, jump and no-edge fallback, repeated Reset and live-builder shutdown");
}
