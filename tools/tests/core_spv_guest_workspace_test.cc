/* Offline real-translator bridge; output can be compared with an older build. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/base/string_buffer.h"
#include <cstdio>
#include <cstdlib>
#include <bit>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  const char* path = std::getenv("SPV_GUEST_OUTPUT");
  CHECK(path);
  FILE* output = std::fopen(path, "wb");
  CHECK(output);
  const uint32_t ucode[3] = {};
  for (unsigned features = 0; features < 2; ++features) {
    xe::gpu::SpirvShaderTranslator translator(
        xe::gpu::SpirvShaderTranslator::Features(features != 0),
        false, false, false, false, 1, 1);
    for (unsigned round = 0; round < 128; ++round) {
      bool pixel = (round & 1) != 0;
      xe::gpu::Shader shader(pixel ? xe::gpu::xenos::ShaderType::kPixel
                                  : xe::gpu::xenos::ShaderType::kVertex,
                            round, ucode, 3, std::endian::native);
      xe::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly);
      uint64_t modification = pixel
          ? translator.GetDefaultPixelShaderModification(4)
          : translator.GetDefaultVertexShaderModification(4);
      auto* translation = shader.GetOrCreateTranslation(modification);
      CHECK(translator.TranslateAnalyzedShader(*translation));
      CHECK(translation->is_valid());
      const auto& words = translation->translated_binary();
      uint32_t size = uint32_t(words.size());
      CHECK(size && std::fwrite(&size, sizeof(size), 1, output) == 1);
      CHECK(std::fwrite(words.data(), 1, size, output) == size);
    }
    // The last builder is still live when the translator is destroyed.
  }
  CHECK(std::fclose(output) == 0);
  std::puts("PASS: 256 real guest translations, alternating vertex/pixel, repeated Reset and live-builder shutdown");
}
