/* C89 Windows frontend: retire Vulkan callbacks before core DLL unload.
 * Args: core DLL, private system directory, synthetic invalid XEX path.
 * Requires a Vulkan 1.3 GPU; does not run guest code or create a window. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <libretro.h>
#include <libretro_vulkan.h>
typedef void (RETRO_CALLCONV *SetEnvironment)(retro_environment_t);
typedef void (RETRO_CALLCONV *Lifecycle)(void);
typedef bool (RETRO_CALLCONV *LoadGame)(const struct retro_game_info *);
static const char *system_dir;
static struct retro_hw_render_callback hardware;
static struct retro_hw_render_context_negotiation_interface_vulkan negotiation;
static unsigned negotiated;
/* 0: no support query; 1: v1; 2: v2; 3: reject v2 and accept v1;
 * 4: v2 wrapper fails. */
static unsigned negotiation_mode;
static PFN_vkCreateDevice frontend_create;
static unsigned wrapper_calls;
static VkDevice frontend_wrapper(VkPhysicalDevice gpu, void *opaque,
                                 const VkDeviceCreateInfo *info) {
  VkDeviceCreateInfo modified;
  VkDevice device = VK_NULL_HANDLE;
  const char *extensions[128];
  unsigned i, count;
  int has_swapchain = 0;
  if (opaque != &wrapper_calls || !info || !info->pNext) return VK_NULL_HANDLE;
  ++wrapper_calls;
  if (negotiation_mode == 4) return VK_NULL_HANDLE;
  count = info->enabledExtensionCount;
  if (count >= 128) return VK_NULL_HANDLE;
  for (i = 0; i < count; ++i) {
    extensions[i] = info->ppEnabledExtensionNames[i];
    if (!strcmp(extensions[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME)) has_swapchain = 1;
  }
  if (!has_swapchain) extensions[count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  modified = *info;
  modified.enabledExtensionCount = count;
  modified.ppEnabledExtensionNames = extensions;
  if (frontend_create(gpu, &modified, NULL, &device) != VK_SUCCESS) return VK_NULL_HANDLE;
  return device;
}
static bool RETRO_CALLCONV environment(unsigned command, void *data) {
  if (command == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY ||
      command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) {
    *(const char **)data = system_dir; return true;
  }
  if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
    ((struct retro_variable *)data)->value = NULL; return false;
  }
  if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true;
  if (command == RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT) {
    struct retro_hw_render_context_negotiation_interface *probe =
        (struct retro_hw_render_context_negotiation_interface *)data;
    if (!negotiation_mode) return false;
    probe->interface_version = negotiation_mode == 1 ? 1 : 2;
    return true;
  }
  if (command == RETRO_ENVIRONMENT_SET_HW_RENDER) {
    hardware = *(struct retro_hw_render_callback *)data;
    return hardware.context_type == RETRO_HW_CONTEXT_VULKAN;
  }
  if (command == RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE) {
    const struct retro_hw_render_context_negotiation_interface_vulkan *incoming =
        (const struct retro_hw_render_context_negotiation_interface_vulkan *)data;
    ++negotiated;
    if (negotiation_mode == 3 && incoming->interface_version >= 2) return false;
    memset(&negotiation, 0, sizeof(negotiation));
    memcpy(&negotiation, incoming, incoming->interface_version >= 2
        ? sizeof(negotiation)
        : offsetof(struct retro_hw_render_context_negotiation_interface_vulkan, create_instance));
    return true;
  }
  return false;
}
int main(int argc, char **argv) {
  HMODULE module, loader, still_loaded;
  FARPROC symbol;
  PFN_vkVoidFunction vk_symbol;
  PFN_vkGetInstanceProcAddr get_proc;
  PFN_vkCreateInstance create_instance;
  PFN_vkDestroyInstance destroy_instance;
  PFN_vkDestroyDevice destroy_device;
  PFN_vkDeviceWaitIdle wait_device;
  PFN_vkEnumeratePhysicalDevices enumerate_devices;
  PFN_vkGetDeviceProcAddr get_device_proc;
  SetEnvironment set_environment;
  Lifecycle init, deinit, unload;
  LoadGame load;
  struct retro_game_info game;
  struct retro_vulkan_context context;
  VkInstance instance;
  VkInstanceCreateInfo instance_info;
  VkPhysicalDeviceFeatures features;
  unsigned cycle;
  unsigned gpu_count = 1;
  VkPhysicalDevice gpu = VK_NULL_HANDLE;
  const char *instance_extensions[1] = { VK_KHR_SURFACE_EXTENSION_NAME };
  bool created;
  if (argc != 4 && argc != 5) return 2;
  if (argc == 5) negotiation_mode = (unsigned)atoi(argv[4]);
  if (negotiation_mode > 4) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  system_dir = argv[2];
  module = LoadLibraryA(argv[1]); if (!module) return 3;
#define FIND(function, name) do { symbol = GetProcAddress(module, name); if (!symbol) return 4; memcpy(&function, &symbol, sizeof(function)); } while (0)
  FIND(set_environment, "retro_set_environment"); FIND(init, "retro_init");
  FIND(deinit, "retro_deinit"); FIND(unload, "retro_unload_game");
  FIND(load, "retro_load_game");
  set_environment(environment); init();
  memset(&game, 0, sizeof(game)); game.path = argv[3];
  if (!load(&game) || negotiated != (negotiation_mode == 3 ? 2u : 1u) || !negotiation.create_device) return 5;
  if (negotiation.interface_version !=
      (negotiation_mode == 2 || negotiation_mode == 4 ? 2u : 1u)) return 16;
  if (negotiation.interface_version >= 2 && !negotiation.create_device2) return 17;
  if (negotiation.destroy_device) {
    fputs("Frontend would retain a core destroy callback past DLL unload\n", stderr);
    return 6;
  }
  loader = LoadLibraryA("vulkan-1.dll"); if (!loader) return 7;
  symbol = GetProcAddress(loader, "vkGetInstanceProcAddr");
  if (!symbol) return 8;
  memcpy(&get_proc, &symbol, sizeof(get_proc));
#define VK_FIND(function, name) do { vk_symbol = get_proc(instance, name); if (!vk_symbol) return 9; memcpy(&function, &vk_symbol, sizeof(function)); } while (0)
  instance = VK_NULL_HANDLE;
  VK_FIND(create_instance, "vkCreateInstance");
  memset(&instance_info, 0, sizeof(instance_info));
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = negotiation.get_application_info();
  instance_info.enabledExtensionCount = 1;
  instance_info.ppEnabledExtensionNames = instance_extensions;
  if (create_instance(&instance_info, NULL, &instance) != VK_SUCCESS) return 10;
  VK_FIND(destroy_instance, "vkDestroyInstance");
  VK_FIND(destroy_device, "vkDestroyDevice");
  VK_FIND(wait_device, "vkDeviceWaitIdle");
  VK_FIND(frontend_create, "vkCreateDevice");
  VK_FIND(enumerate_devices, "vkEnumeratePhysicalDevices");
  VK_FIND(get_device_proc, "vkGetDeviceProcAddr");
  if (enumerate_devices(instance, &gpu_count, &gpu) != VK_SUCCESS || !gpu_count) return 18;
  memset(&features, 0, sizeof(features));
  for (cycle = 0; cycle < 2; ++cycle) {
    memset(&context, 0, sizeof(context));
    if (negotiation.interface_version >= 2) {
      created = negotiation.create_device2(&context, instance, gpu,
          VK_NULL_HANDLE, get_proc, frontend_wrapper, &wrapper_calls);
    } else {
      created = negotiation.create_device(&context, instance, gpu,
          VK_NULL_HANDLE, get_proc, NULL, 0, NULL, 0, &features);
    }
    if (negotiation_mode == 4) {
      if (created || wrapper_calls != cycle + 1 || context.device) return 19;
      if (cycle == 1) {
        unload(); deinit();
        if (!FreeLibrary(module)) return 13;
      }
      continue;
    }
    if (!created) return 11;
    if (!context.device || !context.queue) return 12;
    if (negotiation.interface_version >= 2 &&
        (wrapper_calls != cycle + 1 || !get_device_proc(context.device, "vkCreateSwapchainKHR"))) return 20;
    if (!cycle) {
      /* Context reset path: retire the wrappers while the core remains loaded. */
      hardware.context_destroy();
    } else {
      /* Content close path: destroy the actual Vulkan device after DLL unload. */
      unload(); deinit();
      if (!FreeLibrary(module)) return 13;
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          (LPCSTR)module, &still_loaded)) return 14;
    }
    if (wait_device(context.device) != VK_SUCCESS) return 15;
    destroy_device(context.device, NULL);
  }
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      (LPCSTR)module, &still_loaded)) return 14;
  destroy_instance(instance, NULL);
  FreeLibrary(loader);
  printf("PASS: Vulkan negotiation mode %u, frontend wrapper/extensions, ownership and DLL unload\n", negotiation_mode);
  return 0;
}
