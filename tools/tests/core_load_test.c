/* Load a Windows core and verify the libretro ABI. MSVC C89-compatible. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
  HMODULE module;
  FARPROC symbol;
  ApiVersion version;
  GetInfo get_info;
  Lifecycle init, deinit;
  struct retro_system_info info;
  unsigned i;
  int result = 1;
  if (argc != 2 && argc != 3) return 2;
  if (argc == 3 && strcmp(argv[2], "lifecycle")) return 2;
  module = LoadLibraryA(argv[1]);
  if (!module) {
    fprintf(stderr, "LoadLibrary failed: %lu\n", (unsigned long)GetLastError());
    return 1;
  }
  for (i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
    if (!GetProcAddress(module, required[i])) {
      fprintf(stderr, "Missing export: %s\n", required[i]); goto done;
    }
  }
  symbol = GetProcAddress(module, "retro_api_version"); memcpy(&version, &symbol, sizeof(version));
  symbol = GetProcAddress(module, "retro_get_system_info"); memcpy(&get_info, &symbol, sizeof(get_info));
  if (version() != RETRO_API_VERSION) goto done;
  memset(&info, 0, sizeof(info)); get_info(&info);
  if (!info.library_name || strcmp(info.library_name, "parallel_xbox360")) goto done;
  if (argc == 3) {
    symbol = GetProcAddress(module, "retro_init"); memcpy(&init, &symbol, sizeof(init));
    symbol = GetProcAddress(module, "retro_deinit"); memcpy(&deinit, &symbol, sizeof(deinit));
    for (i = 0; i < 2; ++i) { init(); deinit(); }
  }
  result = 0;
done:
  if (!FreeLibrary(module)) result = 1;
  if (!result) puts("Core load/unload: 25 exports and parallel_xbox360 metadata matched.");
  return result;
}
