/* C89 Windows frontend: D3D12 context callbacks and frontend COM lifetime.
 * Args: core DLL, private system directory, synthetic invalid XEX path. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <libretro.h>
#include <libretro_d3d12.h>
typedef void (RETRO_CALLCONV *SetEnvironment)(retro_environment_t);
typedef void (RETRO_CALLCONV *Lifecycle)(void);
typedef bool (RETRO_CALLCONV *LoadGame)(const struct retro_game_info *);
static const char *system_dir;
static struct retro_hw_render_callback hardware;
static struct retro_hw_render_interface_d3d12 frontend;
static unsigned context_requests, negotiation_requests;
static void set_texture(void *handle, ID3D12Resource *texture, DXGI_FORMAT format) {
  (void)handle; (void)texture; (void)format;
}
static bool RETRO_CALLCONV environment(unsigned command, void *data) {
  if (command == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY ||
      command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) {
    *(const char **)data = system_dir; return true;
  }
  if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
    struct retro_variable *variable = (struct retro_variable *)data;
    variable->value = !strcmp(variable->key, "xenia_graphics_api") ? "d3d12" : NULL;
    return variable->value != NULL;
  }
  if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true;
  if (command == RETRO_ENVIRONMENT_SET_HW_RENDER) {
    hardware = *(struct retro_hw_render_callback *)data;
    ++context_requests;
    return hardware.context_type == RETRO_HW_CONTEXT_D3D12;
  }
  if (command == RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE) {
    ++negotiation_requests; return false;
  }
  if (command == RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE) {
    *(const struct retro_hw_render_interface_d3d12 **)data = &frontend;
    return true;
  }
  return false;
}
int main(int argc, char **argv) {
  HMODULE module, still_loaded;
  FARPROC symbol;
  SetEnvironment set_environment;
  Lifecycle init, deinit, unload;
  LoadGame load;
  struct retro_game_info game;
  D3D12_COMMAND_QUEUE_DESC queue_info;
  ID3D12Fence *fence = NULL;
  HANDLE event;
  unsigned cycle;
  HRESULT hr;
  if (argc != 4) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  system_dir = argv[2];
  memset(&frontend, 0, sizeof(frontend));
  frontend.interface_type = RETRO_HW_RENDER_INTERFACE_D3D12;
  frontend.interface_version = RETRO_HW_RENDER_INTERFACE_D3D12_VERSION;
  frontend.set_texture = set_texture;
  hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&frontend.device);
  if (FAILED(hr)) return 3;
  memset(&queue_info, 0, sizeof(queue_info)); queue_info.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  hr = ID3D12Device_CreateCommandQueue(frontend.device, &queue_info, &IID_ID3D12CommandQueue, (void **)&frontend.queue);
  if (FAILED(hr)) return 4;
  module = LoadLibraryA(argv[1]); if (!module) return 5;
#define FIND(function, name) do { symbol = GetProcAddress(module, name); if (!symbol) return 6; memcpy(&function, &symbol, sizeof(function)); } while (0)
  FIND(set_environment, "retro_set_environment"); FIND(init, "retro_init");
  FIND(deinit, "retro_deinit"); FIND(unload, "retro_unload_game"); FIND(load, "retro_load_game");
  set_environment(environment);
  for (cycle = 0; cycle < 2; ++cycle) {
    init(); memset(&game, 0, sizeof(game)); game.path = argv[3];
    if (load(&game)) return 7;
    if (hardware.context_type != RETRO_HW_CONTEXT_D3D12 || !hardware.context_reset || !hardware.context_destroy) return 8;
    if (negotiation_requests || context_requests != cycle + 1) return 9;
    hardware.context_reset(); hardware.context_destroy(); hardware.context_destroy();
    hardware.context_reset();
    unload(); deinit();
    if (FAILED(ID3D12Device_GetDeviceRemovedReason(frontend.device))) return 10;
  }
  if (!FreeLibrary(module)) return 11;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)module, &still_loaded)) return 12;
  /* Exercise the frontend's device and queue after all core code has gone. */
  hr = ID3D12Device_CreateFence(frontend.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
  if (FAILED(hr)) return 13;
  event = CreateEventA(NULL, FALSE, FALSE, NULL); if (!event) return 14;
  if (FAILED(ID3D12Fence_SetEventOnCompletion(fence, 1, event)) ||
      FAILED(ID3D12CommandQueue_Signal(frontend.queue, fence, 1)) ||
      WaitForSingleObject(event, 5000) != WAIT_OBJECT_0) return 15;
  CloseHandle(event); ID3D12Fence_Release(fence);
  ID3D12CommandQueue_Release(frontend.queue); ID3D12Device_Release(frontend.device);
  puts("PASS: D3D12 context cycles, no retained negotiation callbacks, verified core unload and post-unload frontend queue execution");
  return 0;
}
