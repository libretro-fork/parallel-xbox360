/* Fixed prepared integer transfer PSOs. Initialization/teardown only; C89. */
#ifndef XE_CORE_TRANSFER_UINT_VK_H
#define XE_CORE_TRANSFER_UINT_VK_H
#include <vulkan/vulkan_core.h>
#include "core_transfer_uint_spv.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XE_TRANSFER_UINT_VK_PIPELINES=336};
typedef struct xe_transfer_uint_vk_ops {
 PFN_vkCreateShaderModule create_shader;
 PFN_vkDestroyShaderModule destroy_shader;
 PFN_vkCreatePipelineLayout create_layout;
 PFN_vkDestroyPipelineLayout destroy_layout;
 PFN_vkCreateGraphicsPipelines create_pipelines;
 PFN_vkDestroyPipeline destroy_pipeline;
} xe_transfer_uint_vk_ops;
typedef struct xe_transfer_uint_vk_config {
 VkDevice device;
 VkDescriptorSetLayout image_layout;
 VkShaderModule vertex;
 /* Borrowed compatible one-color passes: class0/1/2/3 is RG16/RGBA16/R32/RG32
  * UINT. Column is guest sample enum0/1/2, with global native2 policy applied.
  * Only consulted during initialization; not owned by the bank. */
 VkRenderPass passes[4][3];
 unsigned native2,sample_shading,dynamic_rendering;
 unsigned scale_x,scale_y;
} xe_transfer_uint_vk_config;
typedef struct xe_transfer_uint_vk_bank {
 VkDevice device;
 VkPipelineLayout layout;
 /* Dense device output: no temporary handle array or map insertion/copy. */
 VkPipeline pipeline[XE_TRANSFER_UINT_VK_PIPELINES];
 unsigned native2,sample_shading,scale_classes,ready;
} xe_transfer_uint_vk_bank;
/* Zero-initialize bank. Scratch is borrowed and may grow at initialization.
 * Device, image layout and vertex are borrowed. Initialization owns/destroys
 * its temporary modules and publishes ready only after all PSOs exist.
 * One native/scaled pair creates four scale classes, collapsed to one at1x.
 * Failure closes all partial driver outputs. No GPU waits or logging. */
int xe_transfer_uint_vk_init(xe_transfer_uint_vk_bank *bank,
 const xe_transfer_uint_vk_config *config,const xe_transfer_uint_vk_ops *ops,
 xe_spv_binary *scratch);
/* Caller must have completed all GPU uses, using its existing retirement path.
 * Returns0 for missing destruction operations without discarding ownership. */
int xe_transfer_uint_vk_close(xe_transfer_uint_vk_bank *bank,
 const xe_transfer_uint_vk_ops *ops);
/* Lookup only. dest_format is the actual integer-view guest format. Source
 * and destination sample enums are0/1/2. Native flags select each image's1x
 * scale rather than the initialization scale. Returns1 pipeline with sample shading, otherwise
 * 1<<dest_msaa contiguous pipelines indexed by LOGICAL destination sample.
 * Emulated2x logical sample1 covers physical sample3. No creation or admission. */
const VkPipeline *xe_transfer_uint_vk_get(const xe_transfer_uint_vk_bank *bank,
 unsigned dest_format,unsigned dest_msaa,unsigned source_msaa,
 unsigned dest_native,unsigned source_native);
unsigned xe_transfer_uint_vk_physical_sample(const xe_transfer_uint_vk_bank *bank,
 unsigned dest_msaa,unsigned logical_sample);
#ifdef __cplusplus
}
#endif
#endif
