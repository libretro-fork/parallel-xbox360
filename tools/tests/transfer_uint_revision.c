/* Compile separately against each revision. Never linked into the core. */
#define VK_NO_PROTOTYPES
#include "transfer_uint_revision.h"
#include "libretro/core_transfer_uint_vk.h"
#include <stdlib.h>
typedef struct revision_owner {
 xe_transfer_uint_vk_bank bank;
 xe_transfer_uint_vk_ops ops;
} revision_owner;
/* A schema change must update the harness, not silently share wrong packets. */
typedef char packet_schema_size[(sizeof(xe_transfer_uint_packet)==XE_TEST_TRANSFER_PACKET_BYTES)?1:-1];
typedef char packet_schema_source[(offsetof(xe_transfer_uint_packet,source)==92)?1:-1];
typedef char packet_schema_dest[(offsetof(xe_transfer_uint_packet,dest)==100)?1:-1];
typedef char packet_schema_sample[(offsetof(xe_transfer_uint_packet,sample)==108)?1:-1];
static void *revision_open(const xe_test_transfer_revision_config *input) {
 revision_owner *owner;xe_transfer_uint_vk_config config={0};xe_spv_binary scratch={0};
 unsigned f,m;int ready;
 if(!input || input->sample_shading!=1)return NULL;
 owner=(revision_owner*)calloc(1,sizeof(*owner));if(!owner)return NULL;
 config.device=input->device;config.image_layout=input->images;config.vertex=input->vertex;
 config.native2=input->native2;config.sample_shading=input->sample_shading;
 config.dynamic_rendering=input->dynamic_rendering;config.scale_x=input->scale_x;config.scale_y=input->scale_y;
 for(f=0;f<4;++f)for(m=0;m<3;++m)config.passes[f][m]=input->passes[f][m];
 owner->ops.create_shader=input->create_shader;owner->ops.destroy_shader=input->destroy_shader;
 owner->ops.create_layout=input->create_layout;owner->ops.destroy_layout=input->destroy_layout;
 owner->ops.create_pipelines=input->create_pipelines;owner->ops.destroy_pipeline=input->destroy_pipeline;
 ready=xe_transfer_uint_vk_init(&owner->bank,&config,&owner->ops,&scratch);
 xe_spv_binary_close(&scratch);
 if(!ready){free(owner);return NULL;}
 return owner;
}
static VkPipelineLayout revision_layout(const void *opaque) {
 const revision_owner *owner=(const revision_owner*)opaque;
 return owner?owner->bank.layout:VK_NULL_HANDLE;
}
static VkPipeline revision_pipeline(const void *opaque,unsigned format,unsigned dest_msaa,unsigned source_msaa) {
 const revision_owner *owner=(const revision_owner*)opaque;const VkPipeline *pipeline;
 if(!owner)return VK_NULL_HANDLE;
 pipeline=xe_transfer_uint_vk_get(&owner->bank,format,dest_msaa,source_msaa,0,0);
 return pipeline?pipeline[0]:VK_NULL_HANDLE;
}
static int revision_packet(const xe_test_transfer_revision_key *input,void *out,size_t bytes) {
 xe_transfer_address_key key={0};xe_transfer_uint_options options={0};
 if(!input || !out || bytes!=sizeof(xe_transfer_uint_packet))return 0;
 key.dest_msaa=input->dest_msaa;key.source_msaa=input->source_msaa;
 key.dest_native2=input->dest_native2;key.source_native2=input->source_native2;
 key.dest_scale_x=input->dest_scale_x;key.dest_scale_y=input->dest_scale_y;
 key.source_scale_x=input->source_scale_x;key.source_scale_y=input->source_scale_y;
 key.dest_wide=input->dest_wide;key.source_wide=input->source_wide;
 key.dest_pitch=input->dest_pitch;key.source_pitch=input->source_pitch;
 key.dest_to_source_base=input->delta;key.dest_color=key.source_color=1;
 options.source_multisampled=key.source_msaa!=0;
 options.sample_mode=key.dest_msaa?XE_TRANSFER_UINT_SAMPLE_BUILTIN:XE_TRANSFER_UINT_SINGLE_SAMPLE;
 return xe_transfer_uint_packet_prepare((xe_transfer_uint_packet*)out,&options,&key,input->source_format,input->dest_format,input->sample);
}
static int revision_close(void *opaque) {
 revision_owner *owner=(revision_owner*)opaque;
 if(!owner)return 1;
 if(!xe_transfer_uint_vk_close(&owner->bank,&owner->ops))return 0;
 free(owner);return 1;
}
#ifdef _WIN32
#define XE_TEST_REVISION_EXPORT __declspec(dllexport)
#else
#define XE_TEST_REVISION_EXPORT __attribute__((visibility("default")))
#endif
XE_TEST_REVISION_EXPORT const xe_test_transfer_revision_api *xe_test_transfer_revision_get(unsigned version,size_t bytes) {
 static const xe_test_transfer_revision_api api={XE_TEST_TRANSFER_REVISION_ABI,XE_TEST_TRANSFER_PACKET_BYTES,
  revision_open,revision_layout,revision_pipeline,revision_packet,revision_close};
 return version==api.version && bytes==sizeof(api)?&api:NULL;
}
