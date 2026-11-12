/* Finish writes to an allocation mapped from offset zero through VK_WHOLE_SIZE.
 * Borrow the actual upload allocation; never flush its device-local copy.
 * MSVC C89. No retained handles or callbacks. */
#ifndef XE_CORE_VK_UPLOAD_FLUSH_H
#define XE_CORE_VK_UPLOAD_FLUSH_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
#include <vulkan/vulkan.h>
static INLINE VkResult xe_vk_upload_flush(PFN_vkFlushMappedMemoryRanges flush,
      VkDevice device,VkDeviceMemory upload,uint32_t memory_type,
      uint32_t coherent_types)
{
   VkMappedMemoryRange range;
   if(device==VK_NULL_HANDLE || upload==VK_NULL_HANDLE || memory_type>=32)
      return VK_ERROR_MEMORY_MAP_FAILED;
   if(coherent_types&(UINT32_C(1)<<memory_type))return VK_SUCCESS;
   if(!flush)return VK_ERROR_MEMORY_MAP_FAILED;
   range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.pNext=NULL;
   range.memory=upload;
   range.offset=0;
   range.size=VK_WHOLE_SIZE;
   return flush(device,1,&range);
}
#endif
