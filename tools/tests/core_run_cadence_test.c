/* Exercise retro_run without a mixer: one video and one full silent audio frame.
 * MSVC C89. Arg: native core. No game, GPU or private files required. */
#include "core_test_module.h"
#include <stdio.h>
#include <string.h>
#include <libretro.h>
typedef void (RETRO_CALLCONV *SetEnvironment)(retro_environment_t);
typedef void (RETRO_CALLCONV *SetVideo)(retro_video_refresh_t);
typedef void (RETRO_CALLCONV *SetAudioBatch)(retro_audio_sample_batch_t);
typedef void (RETRO_CALLCONV *SetAudio)(retro_audio_sample_t);
typedef void (RETRO_CALLCONV *Lifecycle)(void);
static unsigned pictures, batches, individual, bad;
static size_t samples;
static bool RETRO_CALLCONV environment(unsigned command, void *data) {
  (void)command; (void)data; return false;
}
static void RETRO_CALLCONV video(const void *data, unsigned width, unsigned height, size_t pitch) {
  (void)data; (void)pitch;
  if (!width || !height) bad = 1;
  ++pictures;
}
static size_t RETRO_CALLCONV audio_batch(const int16_t *data, size_t frames) {
  size_t i;
  if (frames != 800 || !data) bad = 2;
  for (i = 0; i < frames * 2; ++i) if (data[i]) bad = 3;
  ++batches; samples += frames; return frames;
}
static void RETRO_CALLCONV audio(int16_t left, int16_t right) {
  if (left || right) bad = 4;
  ++individual;
}
int main(int argc, char **argv) {
  CoreModule module;
  CoreSymbol symbol;
  SetEnvironment set_environment;
  SetVideo set_video;
  SetAudioBatch set_batch;
  SetAudio set_audio;
  Lifecycle init, run, deinit;
  unsigned i;
  if (argc != 2) return 2;
  module = CORE_LOAD(argv[1]); if (!module) return 3;
#define FIND(function, name) do { symbol = CORE_FIND(module, name); if (!symbol) return 4; memcpy(&function, &symbol, sizeof(function)); } while (0)
  FIND(set_environment, "retro_set_environment"); FIND(set_video, "retro_set_video_refresh");
  FIND(set_batch, "retro_set_audio_sample_batch"); FIND(set_audio, "retro_set_audio_sample");
  FIND(init, "retro_init"); FIND(run, "retro_run"); FIND(deinit, "retro_deinit");
  set_environment(environment); set_video(video); set_batch(audio_batch); set_audio(audio);
  init();
  for (i = 0; i < 120; ++i) {
    run();
    if (pictures != i + 1 || batches != i + 1 || samples != (size_t)(i + 1) * 800 || bad || individual) {
      fprintf(stderr, "retro_run cadence failed: video=%u batches=%u samples=%lu bad=%u\n", pictures, batches, (unsigned long)samples, bad);
      return 5;
    }
  }
  set_batch(NULL);
  for (i = 0; i < 20; ++i) run();
  if (pictures != 140 || batches != 120 || individual != 16000 || bad) return 6;
  deinit();
  if (!CORE_CLOSE(module)) return 7;
  puts("PASS: one video per retro_run, 800 silent audio frames per run, sample-callback fallback");
  return 0;
}
