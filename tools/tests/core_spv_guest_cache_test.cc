/* Offline translator interoperability over user-supplied .xsh microcode.
 * Uses production borrowed VFS ranges; no driver, GPU or game process. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/shader_storage.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  const char* input = std::getenv("SPV_GUEST_CACHE");
  const char* path = std::getenv("SPV_GUEST_OUTPUT");
  CHECK(input && path);
  FILE* output = std::fopen(path, "wb");
  CHECK(output);
  const char* binding_path = std::getenv("SPV_GUEST_BINDINGS_OUTPUT");
  FILE* bindings_output = binding_path ? std::fopen(binding_path, "wb") : nullptr;
  CHECK(!binding_path || bindings_output);
  size_t translated = 0;
  size_t with_switch = 0;
  for (unsigned profile = 0; profile < 2; ++profile) {
    xe::gpu::SpirvShaderTranslator translator(
        xe::gpu::SpirvShaderTranslator::Features(true),
        false, false, profile != 0, false, profile + 1, profile + 1);
    uint64_t valid = xe::gpu::ReadShaderEntries(input,
        [&](xe::gpu::xenos::ShaderType type, const uint32_t* words,
            uint32_t count, uint64_t hash) {
      xe::gpu::SpirvShader shader(type, hash, words, count);
      xe::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly);
      if (!shader.label_addresses().empty()) ++with_switch;
      xe::gpu::SpirvShaderTranslator::Modification modification(
          type == xe::gpu::xenos::ShaderType::kPixel
              ? translator.GetDefaultPixelShaderModification(64)
              : translator.GetDefaultVertexShaderModification(128));
      if (type == xe::gpu::xenos::ShaderType::kPixel) {
        modification.pixel.interpolator_mask = 0xffff;
        modification.pixel.color_targets_used = 15;
      } else {
        modification.vertex.interpolator_mask = 0xffff;
      }
      auto* translation = shader.GetOrCreateTranslation(modification.value);
      CHECK(translator.TranslateAnalyzedShader(*translation));
      CHECK(translation->is_valid());
      CHECK(shader.bindings_ready());
      const auto& textures = shader.GetTextureBindingsAfterTranslation();
      const auto& samplers = shader.GetSamplerBindingsAfterTranslation();
      if (bindings_output) {
        uint32_t counts[3] = {uint32_t(textures.size()), uint32_t(samplers.size()),
            shader.GetUsedTextureMaskAfterTranslation()};
        CHECK(std::fwrite(counts, sizeof(counts), 1, bindings_output) == 1);
        if (!textures.empty()) CHECK(std::fwrite(textures.data(), sizeof(textures[0]), textures.size(), bindings_output) == textures.size());
        if (!samplers.empty()) CHECK(std::fwrite(samplers.data(), sizeof(samplers[0]), samplers.size(), bindings_output) == samplers.size());
      }
      const auto& binary = translation->translated_binary();
      uint32_t bytes = uint32_t(binary.size());
      CHECK(bytes && std::fwrite(&bytes, sizeof(bytes), 1, output) == 1);
      CHECK(std::fwrite(binary.data(), 1, bytes, output) == bytes);
      if (type == xe::gpu::xenos::ShaderType::kPixel)
        modification.pixel.interpolator_mask ^= 1;
      else
        modification.vertex.interpolator_mask ^= 1;
      auto* variant = shader.GetOrCreateTranslation(modification.value);
      CHECK(translator.TranslateAnalyzedShader(*variant) && variant->is_valid());
      const auto& reused_textures = shader.GetTextureBindingsAfterTranslation();
      const auto& reused_samplers = shader.GetSamplerBindingsAfterTranslation();
      CHECK(textures.data() == reused_textures.data() && textures.size() == reused_textures.size());
      CHECK(samplers.data() == reused_samplers.data() && samplers.size() == reused_samplers.size());
      ++translated;
      return true;
    });
    CHECK(valid != UINT64_MAX && valid > sizeof(xe::gpu::ShaderStorageFileHeader));
  }
  CHECK(std::fclose(output) == 0);
  if (bindings_output) CHECK(std::fclose(bindings_output) == 0);
  std::printf("PASS: %zu cached guest translations across host-RT and interlock profiles\n", translated);
  std::printf("Guest translations with control-flow labels: %zu\n", with_switch);
}
