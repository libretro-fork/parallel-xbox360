/* Actual loader/device allocation smoke test; no window, GPU commands or waits. */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "core_vk_heap.h"
#define INSTANCE_FN(name) PFN_##name name = NULL
#define DEVICE_FN(name) PFN_##name name = NULL
#define LOAD_INSTANCE(name) do { PFN_vkVoidFunction symbol = get_instance(instance, #name); memcpy(&name, &symbol, sizeof(name)); if (!name) { error = 2; goto finish; } } while (0)
#define LOAD_DEVICE(name) do { PFN_vkVoidFunction symbol = get_device(device, #name); memcpy(&name, &symbol, sizeof(name)); if (!name) { error = 3; goto finish; } } while (0)
int main(void) {
#ifdef _WIN32
   HMODULE loader;
   FARPROC entry;
#else
   void *loader, *entry;
#endif
   PFN_vkGetInstanceProcAddr get_instance = NULL;
   PFN_vkGetDeviceProcAddr get_device = NULL;
   INSTANCE_FN(vkCreateInstance); INSTANCE_FN(vkDestroyInstance);
   INSTANCE_FN(vkEnumeratePhysicalDevices); INSTANCE_FN(vkGetPhysicalDeviceQueueFamilyProperties);
   INSTANCE_FN(vkGetPhysicalDeviceMemoryProperties); INSTANCE_FN(vkCreateDevice);
   DEVICE_FN(vkDestroyDevice); DEVICE_FN(vkAllocateMemory); DEVICE_FN(vkFreeMemory);
   DEVICE_FN(vkCreateImage); DEVICE_FN(vkDestroyImage); DEVICE_FN(vkCreateBuffer); DEVICE_FN(vkDestroyBuffer);
   DEVICE_FN(vkGetImageMemoryRequirements); DEVICE_FN(vkGetBufferMemoryRequirements);
   DEVICE_FN(vkGetImageMemoryRequirements2); DEVICE_FN(vkGetBufferMemoryRequirements2);
   DEVICE_FN(vkBindImageMemory); DEVICE_FN(vkBindBufferMemory);
   VkInstance instance = VK_NULL_HANDLE;
   VkDevice device = VK_NULL_HANDLE;
   VkPhysicalDevice physical[8];
   VkQueueFamilyProperties queues[16];
   VkApplicationInfo app;
   VkInstanceCreateInfo instance_info;
   VkDeviceQueueCreateInfo queue_info;
   VkDeviceCreateInfo device_info;
   xe_vk_heap_info heap_info;
   xe_vk_heap *heap = NULL;
   VkImageCreateInfo image_info;
   VkBufferCreateInfo buffer_info;
   VkImage images[32];
   VkBuffer buffers[32];
   xe_vk_allocation image_memory[32], buffer_memory[32];
   float priority = 1.0f;
   uint32_t count, family, i, round;
   int error = 0;
   memset(images, 0, sizeof(images)); memset(buffers, 0, sizeof(buffers));
#ifdef _WIN32
   loader = LoadLibraryA("vulkan-1.dll");
   if (!loader) return 1;
   entry = GetProcAddress(loader, "vkGetInstanceProcAddr");
#else
   loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
   if (!loader) return 1;
   entry = dlsym(loader, "vkGetInstanceProcAddr");
#endif
   memcpy(&get_instance, &entry, sizeof(get_instance));
   if (!get_instance) { error = 1; goto finish; }
   LOAD_INSTANCE(vkCreateInstance);
   memset(&app, 0, sizeof(app)); app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   app.pApplicationName = "parallel_xbox360 allocator test"; app.apiVersion = VK_API_VERSION_1_1;
   memset(&instance_info, 0, sizeof(instance_info)); instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   instance_info.pApplicationInfo = &app;
   if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS) { error = 4; goto finish; }
   LOAD_INSTANCE(vkDestroyInstance); LOAD_INSTANCE(vkEnumeratePhysicalDevices);
   LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties); LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
   LOAD_INSTANCE(vkCreateDevice);
   {
      PFN_vkVoidFunction symbol = get_instance(instance, "vkGetDeviceProcAddr");
      memcpy(&get_device, &symbol, sizeof(get_device));
   }
   if (!get_device) { error = 5; goto finish; }
   count = 8;
   if (vkEnumeratePhysicalDevices(instance, &count, physical) < 0 || !count) { error = 6; goto finish; }
   count = 16; vkGetPhysicalDeviceQueueFamilyProperties(physical[0], &count, queues);
   for (family = 0; family < count; ++family) if (queues[family].queueCount && (queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) break;
   if (family == count) { error = 7; goto finish; }
   memset(&queue_info, 0, sizeof(queue_info)); queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
   memset(&device_info, 0, sizeof(device_info)); device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
   if (vkCreateDevice(physical[0], &device_info, NULL, &device) != VK_SUCCESS) { error = 8; goto finish; }
   LOAD_DEVICE(vkDestroyDevice); LOAD_DEVICE(vkAllocateMemory); LOAD_DEVICE(vkFreeMemory);
   LOAD_DEVICE(vkCreateImage); LOAD_DEVICE(vkDestroyImage); LOAD_DEVICE(vkCreateBuffer); LOAD_DEVICE(vkDestroyBuffer);
   LOAD_DEVICE(vkGetImageMemoryRequirements); LOAD_DEVICE(vkGetBufferMemoryRequirements);
   LOAD_DEVICE(vkGetImageMemoryRequirements2); LOAD_DEVICE(vkGetBufferMemoryRequirements2);
   LOAD_DEVICE(vkBindImageMemory); LOAD_DEVICE(vkBindBufferMemory);
   memset(&heap_info, 0, sizeof(heap_info));
   heap_info.device = device; heap_info.block_size = 1 << 20; heap_info.max_blocks = 16;
   heap_info.max_spans = 1024; heap_info.dedicated_enabled = 1;
   vkGetPhysicalDeviceMemoryProperties(physical[0], &heap_info.memory);
   heap_info.functions.allocate = vkAllocateMemory; heap_info.functions.free = vkFreeMemory;
   heap_info.functions.create_image = vkCreateImage; heap_info.functions.destroy_image = vkDestroyImage;
   heap_info.functions.create_buffer = vkCreateBuffer; heap_info.functions.destroy_buffer = vkDestroyBuffer;
   heap_info.functions.image_requirements = vkGetImageMemoryRequirements; heap_info.functions.buffer_requirements = vkGetBufferMemoryRequirements;
   heap_info.functions.image_requirements2 = vkGetImageMemoryRequirements2; heap_info.functions.buffer_requirements2 = vkGetBufferMemoryRequirements2;
   heap_info.functions.bind_image = vkBindImageMemory; heap_info.functions.bind_buffer = vkBindBufferMemory;
   heap = xe_vk_heap_create(&heap_info);
   if (!heap || xe_vk_heap_warm(heap) != VK_SUCCESS) { error = 9; goto finish; }
   memset(&image_info, 0, sizeof(image_info)); image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
   image_info.extent.width = image_info.extent.height = 32; image_info.extent.depth = 1;
   image_info.mipLevels = image_info.arrayLayers = 1; image_info.samples = VK_SAMPLE_COUNT_1_BIT;
   image_info.tiling = VK_IMAGE_TILING_OPTIMAL; image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
   memset(&buffer_info, 0, sizeof(buffer_info)); buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   buffer_info.size = 4096; buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   for (round = 0; round < 100; ++round) {
      for (i = 0; i < 32; ++i)
         if (xe_vk_heap_create_image(heap, &image_info, &images[i], &image_memory[i]) != VK_SUCCESS ||
             xe_vk_heap_create_buffer(heap, &buffer_info, &buffers[i], &buffer_memory[i]) != VK_SUCCESS) { error = 10; goto finish; }
      for (i = 0; i < 32; ++i) {
         if (image_memory[i].memory == buffer_memory[i].memory ||
             !xe_vk_heap_destroy_image(heap, images[i], &image_memory[i]) ||
             !xe_vk_heap_destroy_buffer(heap, buffers[i], &buffer_memory[i])) { error = 11; goto finish; }
         images[i] = VK_NULL_HANDLE; buffers[i] = VK_NULL_HANDLE;
      }
   }
finish:
   if (heap) {
      for (i = 0; i < 32; ++i) {
         if (images[i]) xe_vk_heap_destroy_image(heap, images[i], &image_memory[i]);
         if (buffers[i]) xe_vk_heap_destroy_buffer(heap, buffers[i], &buffer_memory[i]);
      }
      if (!xe_vk_heap_destroy(heap) && !error) error = 12;
   }
   if (device && vkDestroyDevice) vkDestroyDevice(device, NULL);
   if (instance && vkDestroyInstance) vkDestroyInstance(instance, NULL);
#ifdef _WIN32
   FreeLibrary(loader);
#else
   dlclose(loader);
#endif
   if (error) fprintf(stderr, "Vulkan allocator smoke failed: %d\n", error);
   else puts("PASS: real Vulkan device, 3200 image/buffer pairs, warm reuse and clean teardown without GPU commands/waits");
   return error;
}
