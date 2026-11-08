/* Native XMP audio streams. Borrow input; no resampling or quantization. */
#ifndef XE_CORE_MEDIA_AUDIO_H
#define XE_CORE_MEDIA_AUDIO_H
#include <formats/data_transfer.h>
#include <retro_common_api.h>
RETRO_BEGIN_DECLS
typedef struct xe_media_audio xe_media_audio;
/* mapped may be NULL when read_at is supplied. Source outlives this owner. */
xe_media_audio *xe_media_audio_open(const uint8_t *mapped, size_t length,
      data_transfer_read_at_t read_at, void *owner);
void xe_media_audio_close(xe_media_audio *audio);
int xe_media_audio_info(const xe_media_audio *audio, unsigned *rate, unsigned *channels);
/* Borrowed interleaved float output until next read/close. 1=data,0=end,-1=error. */
int xe_media_audio_read(xe_media_audio *audio, const float **pcm, size_t *frames);
RETRO_END_DECLS
#endif
