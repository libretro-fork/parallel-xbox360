/* Native XMP audio streams. MSVC C89. */
#include "libretro/core_media_audio.h"
#include <formats/audio.h>
#include <stdlib.h>
#include <string.h>
#define XE_MEDIA_HEAD 65536u
struct xe_media_audio {
   data_transfer_t *window;
   const uint8_t *base;
   void *decoder;
   size_t length, origin, avail;
   enum audio_type_enum type;
   unsigned rate, channels, output_channels;
   float matrix[8][2];
   float pcm[1536];
};
static int xe_media_extend(xe_media_audio *a, size_t end)
{
   if (end > a->length) return 0;
   if (!a->window) return 1;
   data_transfer_window_rebase(a->window,a->origin);
   if (!data_transfer_window_extend(a->window,end)) return 0;
   if (end > a->avail) a->avail = end;
   return 1;
}
void xe_media_audio_close(xe_media_audio *a)
{
   if (!a) return;
   audio_transfer_free(a->decoder,a->type);
   data_transfer_free(a->window);
   free(a);
}
xe_media_audio *xe_media_audio_open(const uint8_t *mapped, size_t length,
      data_transfer_read_at_t read_at, void *owner)
{
   xe_media_audio *a;
   size_t head, next;
   unsigned i;
   uint64_t asf_header;
   if (length < 16 || (mapped && (uintptr_t)mapped > UINTPTR_MAX-length) ||
       (!mapped && (!read_at || !data_transfer_reserve_supported()))) return NULL;
   a = (xe_media_audio*)calloc(1,sizeof(*a));
   if (!a) return NULL;
   a->length = length; a->base = mapped; a->avail = length;
   head = length < XE_MEDIA_HEAD ? length : XE_MEDIA_HEAD;
   if (!mapped) {
      a->window = data_transfer_open_window_reader(length,head,read_at,owner);
      if (!a->window || !data_transfer_window_is_reserved(a->window)) goto fail;
      a->base = data_transfer_ptr(a->window,NULL); a->avail = head;
   }
   a->type = !memcmp(a->base,"RIFF",4) ||
      !memcmp(a->base,"\x30\x26\xb2\x75\x8e\x66\xcf\x11",8)
      ? AUDIO_TYPE_WMA : AUDIO_TYPE_MP3;
   if (a->type == AUDIO_TYPE_MP3 && !memcmp(a->base,"ID3",3)) {
      const uint8_t *p = a->base;
      if (p[3]<2 || p[3]>4 || ((p[6]|p[7]|p[8]|p[9])&128)) goto fail;
      next = 10 + ((size_t)p[6]<<21) + ((size_t)p[7]<<14) +
         ((size_t)p[8]<<7) + p[9];
      if (p[3]==4 && (p[5]&16)) next += 10;
      if (next >= length) goto fail;
      a->origin = next;
      head = length-next < XE_MEDIA_HEAD ? length : next+XE_MEDIA_HEAD;
      if (!xe_media_extend(a,head)) goto fail;
      if (a->window) data_transfer_window_rebase(a->window,next);
   } else if (a->type == AUDIO_TYPE_WMA && memcmp(a->base,"RIFF",4)) {
      if (length < 24) goto fail;
      asf_header = 0;
      for (i=0;i<8;i++) asf_header |= (uint64_t)a->base[16+i] << (i*8);
      if (asf_header > length || length-asf_header < 50) goto fail;
      if (a->window && !data_transfer_window_grow_keep(a->window,(size_t)asf_header+50)) goto fail;
      if (a->avail < asf_header+50) a->avail = (size_t)asf_header+50;
   }
   a->decoder = audio_transfer_new(a->type);
   if (!a->decoder) goto fail;
   audio_transfer_set_buffer_ptr(a->decoder,a->type,(void*)(a->base+a->origin),length-a->origin);
   audio_transfer_set_avail(a->decoder,a->type,a->avail-a->origin);
   if (!audio_transfer_start(a->decoder,a->type) ||
       !audio_transfer_info(a->decoder,a->type,&a->channels,&a->rate,NULL) ||
       !a->channels || a->channels>8 || !a->rate) goto fail;
   a->output_channels = a->channels==6 ? 6 : 2;
   if (a->channels>2 && a->channels!=6) {
      if (!audio_transfer_stereo_matrix_f32(a->decoder,a->type,a->matrix)) goto fail;
   }
   return a;
fail:
   xe_media_audio_close(a);
   return NULL;
}
int xe_media_audio_info(const xe_media_audio *a, unsigned *rate, unsigned *channels)
{
   if (!a || !rate || !channels) return 0;
   *rate=a->rate; *channels=a->output_channels; return 1;
}
int xe_media_audio_read(xe_media_audio *a, const float **pcm, size_t *frames)
{
   size_t got, tell, next, before, demand, i;
   unsigned c;
   int status;
   if (!a || !pcm || !frames) return -1;
   *pcm=NULL; *frames=0;
   for (;;) {
      tell = audio_transfer_buffer_tell(a->decoder,a->type);
      if (a->window) {
         if (!data_transfer_window_feed_budget(a->window,a->origin+tell,
               XE_MEDIA_HEAD,16384,XE_MEDIA_HEAD,&next)) return -1;
         if (next > a->avail) a->avail=next;
         audio_transfer_set_avail(a->decoder,a->type,a->avail-a->origin);
      }
      before=a->avail;
      status=audio_transfer_read_f32(a->decoder,a->type,a->pcm,
            1536/(a->channels>a->output_channels ? a->channels : a->output_channels),&got);
      if (status < 0) return -1;
      if (got) break;
      if (status==AUDIO_PROCESS_END) return 0;
      if (audio_transfer_buffer_tell(a->decoder,a->type)>tell) continue;
      if (!a->window || a->avail>=a->length) return -1;
      demand=audio_transfer_buffer_read_end(a->decoder,a->type);
      if (demand>a->length-a->origin) return -1;
      next = a->length-a->avail<XE_MEDIA_HEAD ? a->length : a->avail+XE_MEDIA_HEAD;
      if (a->origin+demand>next) next=a->origin+demand;
      if (!xe_media_extend(a,next) || a->avail<=before) return -1;
      audio_transfer_set_avail(a->decoder,a->type,a->avail-a->origin);
   }
   if (a->channels==1) {
      for (i=got;i>0;i--) a->pcm[2*i-1]=a->pcm[2*i-2]=a->pcm[i-1];
   } else if (a->channels!=2 && a->channels!=6) {
      for (i=0;i<got;i++) {
         float left=0,right=0;
         for (c=0;c<a->channels;c++) {
            float value=a->pcm[i*a->channels+c];
            left+=value*a->matrix[c][0]; right+=value*a->matrix[c][1];
         }
         a->pcm[2*i]=left; a->pcm[2*i+1]=right;
      }
   }
   *pcm=a->pcm; *frames=got; return 1;
}
