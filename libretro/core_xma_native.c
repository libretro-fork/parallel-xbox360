/* Native XMA owner and borrowed frame admission. MSVC C89. */
#include "libretro/core_xma_native.h"
int xe_xma_native_init(xe_xma_native_owner *owner)
{
   rwma_config cfg;
   if (!owner || owner->codec) return 0;
   memset(&cfg,0,sizeof(cfg));
   cfg.codec=RWMA_XMA_RAW;cfg.sample_rate=48000;cfg.channels=2;
   if (rwma_create(&owner->codec,&cfg)) return 0;
   owner->rate=owner->channels=owner->damaged=0;
   return 1;
}
void xe_xma_native_dispose(xe_xma_native_owner *owner)
{
   if (!owner) return;
   rwma_free(owner->codec);
   memset(owner,0,sizeof(*owner));
}
int xe_xma_native_configure(xe_xma_native_owner *owner,unsigned rate,unsigned channels)
{
   if (!owner || !owner->codec) return -1;
   if (owner->rate==rate && owner->channels==channels) return 0;
   if (rwma_xma_configure(owner->codec,rate,channels)) return -1;
   owner->rate=rate;owner->channels=channels;owner->damaged=0;
   return 1;
}
int xe_xma_native_decode(xe_xma_native_owner *owner,const xe_xma_raw_span *span,
      rwma_frame *frame)
{
   if (!frame) return 0;
   memset(frame,0,sizeof(*frame));
   if (!owner || !owner->codec || !owner->rate || !span)
      return 0;
   if (owner->damaged) rwma_reset(owner->codec);
   owner->damaged=rwma_decode_xma(owner->codec,span->data,span->size,
         span->bit_offset,span->bit_count,frame)!=RWMA_OK;
   return !owner->damaged;
}
int xe_xma_native_capture(const xe_xma_native_owner *owner,rwma_xma_view *view)
{
   if (!view) return 0;
   memset(view,0,sizeof(*view));
   if (!owner || !owner->codec) return 0;
   return !owner->rate || !rwma_xma_capture(owner->codec,view);
}
