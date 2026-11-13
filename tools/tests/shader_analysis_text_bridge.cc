/* Typed analysis/translation comparison; packed scenarios and timing are C89. */
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/gpu_flags.h"
#include <cstring>
using namespace xe::gpu;
extern "C" size_t xe_analysis_compare(const uint32_t* code,size_t count,unsigned pixel) {
  SpirvShader full(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  SpirvShader quiet(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  SpirvShader guarded(pixel?xenos::ShaderType::kPixel:xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  xe::StringBuffer text,unused;unused.Append("untouched");
  const char* unused_storage=unused.buffer();
  full.AnalyzeUcode(&text);quiet.AnalyzeUcode();guarded.AnalyzeUcode(unused);
  if(!full.is_ucode_analyzed() || !quiet.is_ucode_analyzed() || !text.length() ||
     full.ucode_disassembly()!=text.to_string_view() || !quiet.ucode_disassembly().empty() ||
     !guarded.is_ucode_analyzed() || !guarded.ucode_disassembly().empty() ||
     unused.buffer()!=unused_storage || unused.to_string_view()!="untouched")return 0;
  const auto& a=full.constant_register_map();const auto& b=quiet.constant_register_map();
  if(std::memcmp(a.float_bitmap,b.float_bitmap,sizeof(a.float_bitmap)) ||
     std::memcmp(a.bool_bitmap,b.bool_bitmap,sizeof(a.bool_bitmap)) ||
     std::memcmp(a.vertex_fetch_bitmap,b.vertex_fetch_bitmap,sizeof(a.vertex_fetch_bitmap)) ||
     a.loop_bitmap!=b.loop_bitmap || a.float_count!=b.float_count ||
     a.float_dynamic_addressing!=b.float_dynamic_addressing ||
     full.label_addresses()!=quiet.label_addresses() || full.uses_subroutine_calls()!=quiet.uses_subroutine_calls() ||
     full.cf_pair_index_bound()!=quiet.cf_pair_index_bound() ||
     full.register_static_address_bound()!=quiet.register_static_address_bound() ||
     full.uses_register_dynamic_addressing()!=quiet.uses_register_dynamic_addressing() ||
     full.kills_pixels()!=quiet.kills_pixels() || full.writes_depth()!=quiet.writes_depth() ||
     full.writes_color_targets()!=quiet.writes_color_targets() ||
     full.writes_interpolators()!=quiet.writes_interpolators() ||
     full.writes_point_size_edge_flag_kill_vertex()!=quiet.writes_point_size_edge_flag_kill_vertex() ||
     full.uses_texture_fetch_instruction_results()!=quiet.uses_texture_fetch_instruction_results() ||
     full.point_fetch_coordinate_registers()!=quiet.point_fetch_coordinate_registers() ||
     full.vertex_bindings().size()!=quiet.vertex_bindings().size() ||
     full.texture_bindings().size()!=quiet.texture_bindings().size() ||
     full.cf_memexport_info().size()!=quiet.cf_memexport_info().size() ||
     full.memexport_eM_written()!=quiet.memexport_eM_written() ||
     full.memexport_eM_potentially_written_before_end()!=quiet.memexport_eM_potentially_written_before_end() ||
     full.memexport_stream_constants()!=quiet.memexport_stream_constants())return 0;
  for(uint32_t i=0;i<full.cf_pair_index_bound()*2;++i)
    if(full.GetRegisterComponentsWrittenBeforeReentering(i)!=quiet.GetRegisterComponentsWrittenBeforeReentering(i))return 0;
  SpirvShaderTranslator::Features features(false);features.spirv_version=spv::Spv_1_3;
  SpirvShaderTranslator translator(features,false,false,false,false,1,1);
  auto* af=full.GetOrCreateTranslation(0);auto* aq=quiet.GetOrCreateTranslation(0);
  if(!translator.TranslateAnalyzedShader(*af) || !translator.TranslateAnalyzedShader(*aq))return 0;
  auto x=af->translated_binary();auto y=aq->translated_binary();
  if(x.size()!=y.size() || x.empty() || std::memcmp(x.data(),y.data(),x.size()))return 0;
  return text.length();
}
extern "C" int xe_analysis_dump_switch(const uint32_t* code,size_t count,const char* directory) {
  if(!cvars::dump_shaders.empty())return 0;
  cvars::dump_shaders=directory;
  SpirvShader shader(xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  SpirvShader automatic(xenos::ShaderType::kVertex,0,code,count,std::endian::native);
  xe::StringBuffer text;shader.AnalyzeUcode(text);automatic.AnalyzeUcode();
  const char* storage=automatic.ucode_disassembly().data();automatic.AnalyzeUcode();cvars::dump_shaders.clear();
  return text.length()!=0 && shader.ucode_disassembly()==text.to_string_view() &&
      automatic.ucode_disassembly()==text.to_string_view() && automatic.ucode_disassembly().data()==storage;
}
