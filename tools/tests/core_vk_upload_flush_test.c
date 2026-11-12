/* Actual C89 upload flush recipe with a recording Vulkan dispatch seam. */
#include <stdio.h>
#include <stdlib.h>
#include "libretro/core_vk_upload_flush.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"upload flush line %d\n",__LINE__);exit(1); } } while(0)
static unsigned calls;
static VkDevice expected_device;
static VkDeviceMemory expected_upload;
static VkResult returned;
static VKAPI_ATTR VkResult VKAPI_CALL record_flush(VkDevice device,uint32_t count,
      const VkMappedMemoryRange *ranges)
{
   CHECK(device==expected_device && count==1 && ranges);
   CHECK(ranges[0].sType==VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE && !ranges[0].pNext);
   CHECK(ranges[0].memory==expected_upload && !ranges[0].offset && ranges[0].size==VK_WHOLE_SIZE);
   ++calls;return returned;
}
int main(void)
{
   unsigned type,before;
   expected_device=(VkDevice)(uintptr_t)0x1000;
   expected_upload=(VkDeviceMemory)(uintptr_t)0x2000;
   for(type=0;type<32;++type) {
      returned=VK_SUCCESS;before=calls;
      CHECK(xe_vk_upload_flush(record_flush,expected_device,expected_upload,type,0)==VK_SUCCESS && calls==before+1);
      before=calls;
      CHECK(xe_vk_upload_flush(NULL,expected_device,expected_upload,type,UINT32_C(1)<<type)==VK_SUCCESS && calls==before);
      returned=VK_ERROR_DEVICE_LOST;
      CHECK(xe_vk_upload_flush(record_flush,expected_device,expected_upload,type,0)==returned && calls==before+1);
   }
   before=calls;
   CHECK(xe_vk_upload_flush(NULL,expected_device,expected_upload,0,0)==VK_ERROR_MEMORY_MAP_FAILED);
   CHECK(xe_vk_upload_flush(record_flush,VK_NULL_HANDLE,expected_upload,0,0)==VK_ERROR_MEMORY_MAP_FAILED);
   CHECK(xe_vk_upload_flush(record_flush,expected_device,VK_NULL_HANDLE,0,0)==VK_ERROR_MEMORY_MAP_FAILED);
   CHECK(xe_vk_upload_flush(record_flush,expected_device,expected_upload,32,UINT32_MAX)==VK_ERROR_MEMORY_MAP_FAILED);
   CHECK(xe_vk_upload_flush(record_flush,expected_device,expected_upload,UINT32_MAX,UINT32_MAX)==VK_ERROR_MEMORY_MAP_FAILED);
   CHECK(calls==before);
   puts("PASS: coherent/noncoherent upload ownership, all 32 memory types, driver errors and invalid handles");
   return 0;
}
