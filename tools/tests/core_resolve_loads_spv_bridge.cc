/* Only ABI/type adaptation; test loops and binary inspection live in C89. */
#include "core_resolve_loads_emit.h"
#include "xenia/gpu/edram_dump_shader.h"
extern "C" int xe_test_resolve_spv(const xe_test_resolve_options* p,
                                  xe_test_resolve_inspect inspect, void* context) {
  xe::gpu::EdramDumpShaderKey key;
  xe::gpu::EdramDumpShaderOptions options;
  xe_spv_binary binary = {};
  key.resource_format = p->format; key.is_depth = p->depth;
  key.msaa_samples = xe::gpu::xenos::MsaaSamples(p->msaa);
  key.direct_resolve = p->direct; key.source_scale_native = p->config != 0;
  key.native_layout = p->config == 2;
  key.direct_resolve_3d = p->volume;
  options.resolution_scale_x = p->scale; options.resolution_scale_y = p->scale == 1 ? 1 : (p->scale == 4 ? 4 : 3);
  options.msaa_2x_attachments_supported = p->config != 0;
  options.source_is_uint = !p->depth && ((p->format >= 4 && p->format <= 7) || p->format >= 14);
  options.depth_float24_round = true;
  int result = xe::gpu::BuildEdramDumpShaderSpirv(key, options, binary) &&
      inspect(context, binary.words, binary.count);
  xe_spv_binary_close(&binary); return result;
}
