/* Prepared single-sample color transfer PSOs. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_COLOR_VK_H
#define XE_CORE_TRANSFER_COLOR_VK_H
#include <vulkan/vulkan_core.h>
#include "core_transfer_color_spv.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { XE_TRANSFER_COLOR_VK_PAIRS=44, XE_TRANSFER_COLOR_VK_PIPELINES=176 };
typedef struct xe_transfer_color_vk_ops {
 PFN_vkCreateShaderModule create_shader;
 PFN_vkDestroyShaderModule destroy_shader;
 PFN_vkCreatePipelineLayout create_layout;
 PFN_vkDestroyPipelineLayout destroy_layout;
 PFN_vkCreateGraphicsPipelines create_pipelines;
 PFN_vkDestroyPipeline destroy_pipeline;
} xe_transfer_color_vk_ops;
typedef struct xe_transfer_color_vk_config {
 VkDevice device;
 VkDescriptorSetLayout image_layout;
 VkShaderModule vertex;
 /* Bits/classes: float RGBA8, gamma RGBA16, packed10, packed7e3;
  * integer RG16, RGBA16, R32, RG32. Actual view policy, not guest enum. */
 unsigned enabled_views,dynamic_rendering;
 VkRenderPass passes[8];
 xe_transfer_address_packet_config address;
 /* Optional initialization-only producer of the existing packed-address
  * shader. Writes into borrowed reusable final storage; no shader map. */
 int (*emit_packed)(void *,unsigned,unsigned,unsigned,unsigned,xe_spv_binary *);
 void *emit_context;
} xe_transfer_color_vk_config;
typedef struct xe_transfer_color_vk_bank {
 VkDevice device;
 VkPipelineLayout layout;
 VkPipeline pipeline[XE_TRANSFER_COLOR_VK_PIPELINES];
 unsigned enabled_views,scale_classes,ready,packed_address;
} xe_transfer_color_vk_bank;
/* Zero-initialize bank. All creation is initialization-only. Scratch is
 * borrowed reusable final SPIR-V storage. A packed producer may reuse its own
 * instruction arena during initialization; it must return a complete bounded
 * output and is never retained or called by get(). Other handles are borrowed.
 * Unsupported views are omitted; failure retires every partial owned output. */
int xe_transfer_color_vk_init(xe_transfer_color_vk_bank *,
 const xe_transfer_color_vk_config *,const xe_transfer_color_vk_ops *,xe_spv_binary *);
/* Caller completes all GPU uses first. Missing destroy ops retain ownership. */
int xe_transfer_color_vk_close(xe_transfer_color_vk_bank *,const xe_transfer_color_vk_ops *);
/* Immutable borrowed handle; no creation, allocation, queue operation or wait.
 * Integer/integer and same numeric formats belong to the smaller existing
 * banks and return NULL here. Only remaining single-sample pairs qualify. */
const VkPipeline *xe_transfer_color_vk_get(const xe_transfer_color_vk_bank *,
 unsigned source_format,unsigned dest_format,unsigned dest_native,unsigned source_native);
#ifdef __cplusplus
}
#endif
#endif
