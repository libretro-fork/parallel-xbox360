/* Float negotiation/fallback and unload contract through the real core ABI.
 * MSVC C89. Args: native core, private system directory, synthetic invalid XEX.
 * Accepts hardware setup without resetting it; no GPU or guest code runs. */
#include "core_test_module.h"
#include <stdio.h>
#include <string.h>
#include "core_audio_callbacks.h"
typedef void (RETRO_CALLCONV *SetEnvironment)(retro_environment_t);
typedef void (RETRO_CALLCONV *SetVideo)(retro_video_refresh_t);
typedef void (RETRO_CALLCONV *SetBatch)(retro_audio_sample_batch_t);
typedef void (RETRO_CALLCONV *Lifecycle)(void);
typedef bool (RETRO_CALLCONV *LoadGame)(const struct retro_game_info *);
static const char *system_dir;
static unsigned mode, pictures, calls[4], bad, float_queries, multi_queries;
static size_t RETRO_CALLCONV integer_batch(const int16_t *data, size_t frames) {
  size_t i;
  if (frames != 800) bad = 1;
  for (i = 0; i < frames * 2; ++i) if (data[i]) bad = 2;
  ++calls[0]; return frames;
}
static size_t RETRO_CALLCONV float_batch(const float *data, size_t frames) {
  size_t i;
  if (frames != 800) bad = 3;
  for (i = 0; i < frames * 2; ++i) if (data[i] != 0.0f) bad = 4;
  ++calls[1]; return frames;
}
static size_t RETRO_CALLCONV multi_integer(const int16_t *data, size_t frames,
                                         unsigned channels, unsigned layout) {
  size_t i;
  if (channels != 6 || layout != 0x60f || frames != 800) bad = 5;
  for (i = 0; i < frames * 6; ++i) if (data[i]) bad = 6;
  ++calls[2]; return frames;
}
static size_t RETRO_CALLCONV multi_float(const float *data, size_t frames,
                                       unsigned channels, unsigned layout) {
  size_t i;
  if (channels != 6 || layout != 0x60f || frames != 800) bad = 7;
  for (i = 0; i < frames * 6; ++i) if (data[i] != 0.0f) bad = 8;
  ++calls[3]; return frames;
}
static void RETRO_CALLCONV video(const void *data, unsigned width,
                               unsigned height, size_t pitch) {
  (void)data; (void)pitch;
  if (!width || !height) bad = 9;
  ++pictures;
}
static bool RETRO_CALLCONV environment(unsigned command, void *data) {
  if (command == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY ||
      command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) {
    *(const char **)data = system_dir; return true;
  }
  if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
    struct retro_variable *var = (struct retro_variable *)data;
    var->value = NULL;
    if (!strcmp(var->key, "xenia_audio_channels")) {
      var->value = (mode >= 2 && mode <= 4) || mode == 7 ? "5.1" : "stereo";
      return true;
    }
    return false;
  }
  if (command == RETRO_ENVIRONMENT_SET_HW_RENDER ||
      command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true;
  if (command == RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT) {
    struct retro_audio_sample_float_callback *out =
        (struct retro_audio_sample_float_callback *)data;
    ++float_queries;
    out->batch = mode == 5 ? NULL : float_batch;
    return mode != 0 && mode != 6;
  }
  if (command == RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI) {
    struct retro_audio_sample_multi_callback *out =
        (struct retro_audio_sample_multi_callback *)data;
    ++multi_queries;
    out->batch_float = mode == 2 || mode == 4 ? multi_float : NULL;
    out->batch_int16 = mode == 3 || mode == 4 ? multi_integer : NULL;
    return true;
  }
  return false;
}
int main(int argc, char **argv) {
  CoreModule module;
  CoreSymbol symbol;
  SetEnvironment set_environment;
  SetVideo set_video;
  SetBatch set_batch;
  Lifecycle init, run, unload, deinit;
  LoadGame load;
  struct retro_game_info game;
  unsigned i, j, expected;
  if (argc != 4) return 10;
  system_dir = argv[2];
  module = CORE_LOAD(argv[1]); if (!module) return 11;
#define FIND(function, name) do { symbol = CORE_FIND(module, name); if (!symbol) return 12; memcpy(&function, &symbol, sizeof(function)); } while (0)
  FIND(set_environment, "retro_set_environment"); FIND(set_video, "retro_set_video_refresh");
  FIND(set_batch, "retro_set_audio_sample_batch"); FIND(init, "retro_init");
  FIND(run, "retro_run"); FIND(load, "retro_load_game");
  FIND(unload, "retro_unload_game"); FIND(deinit, "retro_deinit");
  set_environment(environment); set_video(video); set_batch(integer_batch);
  init(); memset(&game, 0, sizeof(game)); game.path = argv[3];
  for (mode = 0; mode < 8; ++mode) {
    memset(calls, 0, sizeof(calls)); pictures = bad = float_queries = multi_queries = 0;
    if (!load(&game)) return 13;
    expected = mode == 2 || mode == 4 ? 3 : mode == 3 ? 2 :
               mode == 1 || mode == 7 ? 1 : 0;
    for (i = 0; i < 20; ++i) run();
    for (j = 0; j < 4; ++j) if (calls[j] != (j == expected ? 20u : 0u)) return 14;
    if (bad || pictures != 20 || float_queries != (expected >= 2 ? 0u : 1u)) return 15;
    if (multi_queries != ((mode >= 2 && mode <= 4) || mode == 7 ? 1u : 0u)) return 16;
    unload(); memset(calls, 0, sizeof(calls));
    run();
    if (calls[0] != 1 || calls[1] || calls[2] || calls[3] || bad) return 17;
  }
  /* Retry while a previous load is still waiting for context reset. */
  mode = 1; float_queries = pictures = bad = 0; memset(calls, 0, sizeof(calls));
  if (!load(&game) || !load(&game)) return 19;
  run();
  if (float_queries != 2 || pictures != 1 || bad || calls[0] ||
      calls[1] != 1 || calls[2] || calls[3]) return 20;
  unload();
  deinit(); if (!CORE_CLOSE(module)) return 18;
  puts("PASS: eight audio formats, exact cadence, callback retirement and pending-context load retry");
  return 0;
}
