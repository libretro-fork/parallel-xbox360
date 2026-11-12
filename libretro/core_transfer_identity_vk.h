/* Owned prepared numeric identity pipelines. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_IDENTITY_VK_H
#define XE_CORE_TRANSFER_IDENTITY_VK_H
#include <vulkan/vulkan_core.h>
#include "core_transfer_identity_spv.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XE_TRANSFER_IDENTITY_VK_PIPELINES=336};
typedef struct xe_transfer_identity_vk_ops {
 PFN_vkCreateShaderModule create_shader;
 PFN_vkDestroyShaderModule destroy_shader;
 PFN_vkCreatePipelineLayout create_layout;
 PFN_vkDestroyPipelineLayout destroy_layout;
 PFN_vkCreateGraphicsPipelines create_pipelines;
 PFN_vkDestroyPipeline destroy_pipeline;
} xe_transfer_identity_vk_ops;
typedef struct xe_transfer_identity_vk_config {
 VkDevice device;
 VkDescriptorSetLayout image_layout;
 VkShaderModule vertex;
 /* Compatible one-color passes, per numeric class and guest sample enum. */
 VkRenderPass passes[XE_TRANSFER_IDENTITY_VIEWS][3];
 unsigned native2,sample_shading,dynamic_rendering,scale_x,scale_y;
 /* Actual sampled/attachment image support, bits0/1/2 for guest sample enums.
  * Zero omits unused/unsupported classes, for example disabled gamma16. */
 unsigned sample_mask[XE_TRANSFER_IDENTITY_VIEWS];
} xe_transfer_identity_vk_config;
typedef struct xe_transfer_identity_vk_bank {
 VkDevice device;
 VkPipelineLayout layout;
 VkPipeline pipeline[XE_TRANSFER_IDENTITY_VK_PIPELINES];
 unsigned sample_mask[XE_TRANSFER_IDENTITY_VIEWS];
 unsigned native2,sample_shading,scale_classes,ready;
} xe_transfer_identity_vk_bank;
/* Zero-initialize bank. Config/ops/vertex/passes borrowed through return.
 * Scratch may grow only during initialization. Each shader module serves
 * all compatible numeric view classes and is released before return.
 * Driver batches write directly into final handle slots; no forwarded array. */
int xe_transfer_identity_vk_init(xe_transfer_identity_vk_bank *bank,
 const xe_transfer_identity_vk_config *config,const xe_transfer_identity_vk_ops *ops,
 xe_spv_binary *scratch);
/* Caller has already completed GPU uses through its existing retirement path. */
int xe_transfer_identity_vk_close(xe_transfer_identity_vk_bank *bank,
 const xe_transfer_identity_vk_ops *ops);
/* Existing handles only. Without sample shading, the returned span has
 * 1<<dest_msaa handles indexed by logical destination sample; emulated2x's
 * logical sample1 covers physical sample3. No emission/creation/admission. */
const VkPipeline *xe_transfer_identity_vk_get(const xe_transfer_identity_vk_bank *bank,
 unsigned view,unsigned dest_msaa,unsigned source_msaa,unsigned dest_native,unsigned source_native);
#ifdef __cplusplus
}
#endif
#endif
