/* Native XMA owner and borrowed frame admission. MSVC C89. */
#ifndef XE_CORE_XMA_NATIVE_H
#define XE_CORE_XMA_NATIVE_H
#include <formats/rwma.h>
#include "libretro/core_xma_payload.h"
RETRO_BEGIN_DECLS
typedef struct xe_xma_native_owner {
   rwma_t *codec;
   unsigned rate,channels,damaged;
   uint8_t scratch[XE_XMA_PACKET_CAPACITY];
} xe_xma_native_owner;
/* Zero initialize. Setup reserves stereo capacity; profiles reuse that storage. */
int xe_xma_native_init(xe_xma_native_owner *owner);
void xe_xma_native_dispose(xe_xma_native_owner *owner);
int xe_xma_native_configure(xe_xma_native_owner *owner,unsigned rate,unsigned channels);
int xe_xma_native_decode(xe_xma_native_owner *owner,const xe_xma_raw_span *span,
      rwma_frame *frame);
int xe_xma_native_capture(const xe_xma_native_owner *owner,rwma_xma_view *view);
RETRO_END_DECLS
#endif
