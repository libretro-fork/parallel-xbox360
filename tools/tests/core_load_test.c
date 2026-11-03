/* Load a native core and verify the libretro ABI. MSVC C89-compatible. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE CoreModule;
typedef FARPROC CoreSymbol;
#define CORE_LOAD(path) LoadLibraryA(path)
#define CORE_FIND(module, name) GetProcAddress(module, name)
#define CORE_CLOSE(module) FreeLibrary(module)
#else
#include <dlfcn.h>
typedef void *CoreModule;
typedef void *CoreSymbol;
#define CORE_LOAD(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define CORE_FIND(module, name) dlsym(module, name)
#define CORE_CLOSE(module) (dlclose(module) == 0)
#endif
#include <stdio.h>
#include <string.h>
#include <libretro.h>
typedef unsigned (RETRO_CALLCONV *ApiVersion)(void);
typedef void (RETRO_CALLCONV *GetInfo)(struct retro_system_info *info);
typedef void (RETRO_CALLCONV *Lifecycle)(void);
int main(int argc, char **argv) {
  static const char *required[] = {
    "retro_api_version", "retro_init", "retro_deinit", "retro_get_system_info",
    "retro_get_system_av_info", "retro_set_environment", "retro_set_video_refresh",
    "retro_set_audio_sample", "retro_set_audio_sample_batch", "retro_set_input_poll",
    "retro_set_input_state", "retro_set_controller_port_device", "retro_reset", "retro_run",
    "retro_serialize_size", "retro_serialize", "retro_unserialize", "retro_cheat_reset",
    "retro_cheat_set", "retro_load_game", "retro_load_game_special", "retro_unload_game",
    "retro_get_region", "retro_get_memory_data", "retro_get_memory_size"
  };
  CoreModule module;
  CoreSymbol symbol;
  ApiVersion version;
  GetInfo get_info;
  Lifecycle init, deinit;
  struct retro_system_info info;
  unsigned i;
  int result = 1;
  if (argc != 2 && argc != 3) return 2;
  if (argc == 3 && strcmp(argv[2], "lifecycle")) return 2;
  module = CORE_LOAD(argv[1]);
  if (!module) {
#if defined(_WIN32)
    fprintf(stderr, "LoadLibrary failed: %lu\n", (unsigned long)GetLastError());
#else
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
#endif
    return 1;
  }
  for (i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
    if (!CORE_FIND(module, required[i])) {
      fprintf(stderr, "Missing export: %s\n", required[i]); goto done;
    }
  }
  symbol = CORE_FIND(module, "retro_api_version"); memcpy(&version, &symbol, sizeof(version));
  symbol = CORE_FIND(module, "retro_get_system_info"); memcpy(&get_info, &symbol, sizeof(get_info));
  if (version() != RETRO_API_VERSION) goto done;
  memset(&info, 0, sizeof(info)); get_info(&info);
  if (!info.library_name || strcmp(info.library_name, "parallel_xbox360")) goto done;
  if (argc == 3) {
    symbol = CORE_FIND(module, "retro_init"); memcpy(&init, &symbol, sizeof(init));
    symbol = CORE_FIND(module, "retro_deinit"); memcpy(&deinit, &symbol, sizeof(deinit));
    for (i = 0; i < 2; ++i) { init(); deinit(); }
  }
  result = 0;
done:
  if (!CORE_CLOSE(module)) result = 1;
  if (!result) puts("Core load/unload: 25 exports and parallel_xbox360 metadata matched.");
  return result;
}
