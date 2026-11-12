/* Narrow ABI to the existing production reference emitter. No new shader math. */
#include "transfer_uint_reference.h"
#include "xenia/gpu/edram_transfer_shader.h"
extern "C" int xe_test_transfer_reference_build(xe_spv_binary *out,const xe_test_transfer_reference_options *input) {
 if (!out || !input) return 0;
 xe::gpu::EdramTransferShaderKey key;
 xe::gpu::EdramTransferShaderOptions options;
 key.mode=xe::gpu::EdramTransferMode::kColorToColor;
 key.source_resource_format=input->source_format;
 key.dest_resource_format=input->dest_format;
 key.source_msaa_samples=xe::gpu::xenos::MsaaSamples(input->source_msaa);
 key.dest_msaa_samples=xe::gpu::xenos::MsaaSamples(input->dest_msaa);
 key.source_scale_native=input->source_native;
 key.dest_scale_native=input->dest_native;
 options.resolution_scale_x=input->scale_x;
 options.resolution_scale_y=input->scale_y;
 options.msaa_2x_attachments_supported=input->native2!=0;
 options.sample_rate_shading_supported=input->shading!=0;
#ifdef XE_TEST_TRANSFER_NUMERIC_REFERENCE
 options.source_color_is_uint=options.dest_color_is_uint=false;
#else
 options.source_color_is_uint=options.dest_color_is_uint=true;
#endif
 return xe::gpu::BuildEdramTransferShaderSpirv(key,options,*out)?1:0;
}
