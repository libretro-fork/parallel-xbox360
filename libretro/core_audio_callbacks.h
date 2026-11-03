/* Compatibility declarations for older frontend SDK headers. */
#ifndef XE_CORE_AUDIO_CALLBACKS_H
#define XE_CORE_AUDIO_CALLBACKS_H
#include "libretro.h"
#ifndef RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI
#define RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI (94 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_AUDIO_SPEAKER_FRONT_LEFT    0x001
#define RETRO_AUDIO_SPEAKER_FRONT_RIGHT   0x002
#define RETRO_AUDIO_SPEAKER_FRONT_CENTER  0x004
#define RETRO_AUDIO_SPEAKER_LOW_FREQUENCY 0x008
#define RETRO_AUDIO_SPEAKER_SIDE_LEFT     0x200
#define RETRO_AUDIO_SPEAKER_SIDE_RIGHT    0x400
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_multi_int16_t)(
      const int16_t *data, size_t frames, unsigned channels, unsigned layout);
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_multi_float_t)(
      const float *data, size_t frames, unsigned channels, unsigned layout);
struct retro_audio_sample_multi_callback
{
   retro_audio_sample_batch_multi_int16_t batch_int16;
   retro_audio_sample_batch_multi_float_t batch_float;
};
#endif
#ifndef RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT
#define RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT (85 | RETRO_ENVIRONMENT_EXPERIMENTAL)
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_float_t)(
      const float *data, size_t frames);
struct retro_audio_sample_float_callback {
    retro_audio_sample_batch_float_t batch;
};
#endif
#endif
