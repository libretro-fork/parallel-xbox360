/* Test-only shared-library boundary for matched revision banks. BSD; C89. */
#ifndef XE_TEST_TRANSFER_REVISION_H
#define XE_TEST_TRANSFER_REVISION_H
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>
enum {XE_TEST_TRANSFER_REVISION_ABI=1, XE_TEST_TRANSFER_PACKET_BYTES=112};
typedef struct xe_test_transfer_revision_config {
 VkDevice device;VkDescriptorSetLayout images;VkShaderModule vertex;
 VkRenderPass passes[4][3];
 PFN_vkCreateShaderModule create_shader;PFN_vkDestroyShaderModule destroy_shader;
 PFN_vkCreatePipelineLayout create_layout;PFN_vkDestroyPipelineLayout destroy_layout;
 PFN_vkCreateGraphicsPipelines create_pipelines;PFN_vkDestroyPipeline destroy_pipeline;
 unsigned native2,sample_shading,dynamic_rendering,scale_x,scale_y;
} xe_test_transfer_revision_config;
typedef struct xe_test_transfer_revision_key {
 uint32_t dest_msaa,source_msaa,dest_native2,source_native2;
 uint32_t dest_scale_x,dest_scale_y,source_scale_x,source_scale_y;
 uint32_t dest_wide,source_wide,dest_pitch,source_pitch;
 int32_t delta;
 unsigned source_format,dest_format,sample;
} xe_test_transfer_revision_key;
typedef struct xe_test_transfer_revision_api {
 unsigned version,packet_bytes;
 void *(*open)(const xe_test_transfer_revision_config *config);
 VkPipelineLayout (*layout)(const void *owner);
 VkPipeline (*pipeline)(const void *owner,unsigned format,unsigned dest_msaa,unsigned source_msaa);
 int (*packet)(const xe_test_transfer_revision_key *key,void *out,size_t bytes);
 /* Caller has completed all GPU uses. Failure retains ownership. */
 int (*close)(void *owner);
} xe_test_transfer_revision_api;
typedef const xe_test_transfer_revision_api *(*xe_test_transfer_revision_entry)(unsigned version,size_t bytes);
#endif
