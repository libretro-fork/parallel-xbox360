/* Typed API glue only. Packed programs and expected dataflow live in C89. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/shader_translator.h"
#include <cstdio>
using namespace xe::gpu;
class MemexportGuardTranslator final : public ShaderTranslator {
 public:
  unsigned started = 0;
  void StartTranslation() override { ++started; }
};
#define CHECK(x) do { if (!(x)) return unsigned(__LINE__); } while (0)
extern "C" unsigned xe_memexport_native_check(const uint32_t* code, size_t count,
 unsigned pixel, const uint8_t* before, size_t cf_count, unsigned end,
 unsigned invalid, const char* path) {
 CHECK(pixel<4);const bool portable=(pixel&2)!=0;pixel&=1;
 Shader shader(pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex,
               0, code, count, std::endian::native);
 shader.AnalyzeUcode();
 CHECK(shader.is_ucode_analyzed());
 if (invalid) {
  CHECK(!shader.is_ucode_analysis_valid());
  auto* translation=shader.GetOrCreateTranslation(0);
  CHECK(translation && translation->TryClaimTranslation());
  MemexportGuardTranslator guard;
  CHECK(!guard.TranslateAnalyzedShader(*translation));
  CHECK(!guard.started && translation->is_translated() && !translation->is_valid());
  CHECK(translation->translated_binary().empty());
  CHECK(!translation->TryClaimTranslation());
  return 0;
 }
 CHECK(shader.is_ucode_analysis_valid());
 CHECK(shader.cf_pair_index_bound()*2==cf_count);
 CHECK(shader.memexport_eM_potentially_written_before_end()==end);
 if(shader.memexport_eM_written()) {
  CHECK(shader.cf_memexport_info().size()==cf_count);
  for(size_t i=0;i<cf_count;++i)
   CHECK(shader.cf_memexport_info()[i].eM_potentially_written_before==before[i]);
 }
 SpirvShaderTranslator::Features features(!portable);
 if(portable){features.spirv_version=spv::Spv_1_3;features.vertex_pipeline_stores_and_atomics=true;
  features.fragment_stores_and_atomics=true;features.max_storage_buffer_range=512u*1024u*1024u;}
 SpirvShaderTranslator translator(features,false,false,false,false,1,1);
 uint64_t modification=pixel?translator.GetDefaultPixelShaderModification(8):translator.GetDefaultVertexShaderModification(8);
 auto* translation=shader.GetOrCreateTranslation(modification);
 CHECK(translation && translator.TranslateAnalyzedShader(*translation));
 CHECK(translation->is_translated() && translation->is_valid());
 const auto bytes=translation->translated_binary();CHECK(!bytes.empty());
 FILE* output=std::fopen(path,"wb");CHECK(output);
 const bool written=std::fwrite(bytes.data(),1,bytes.size(),output)==bytes.size();
 const int closed=std::fclose(output);CHECK(written && !closed);
 return 0;
}
extern "C" unsigned xe_memexport_unanalyzed_rejection(void) {
 Shader shader(xenos::ShaderType::kVertex,0,nullptr,0,std::endian::native);
 auto* translation=shader.GetOrCreateTranslation(0);
 CHECK(translation && translation->TryClaimTranslation());
 MemexportGuardTranslator guard;
 CHECK(!guard.TranslateAnalyzedShader(*translation));
 CHECK(translation->is_translated() && !translation->is_valid() && !guard.started);
 return 0;
}
