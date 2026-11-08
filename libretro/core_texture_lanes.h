/* Compile-time lane selection for componentwise texture conversion. C89.
 * Components borrow the caller's original four-lane values; no payload moves. */
#ifndef XE_CORE_TEXTURE_LANES_H
#define XE_CORE_TEXTURE_LANES_H
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_texture_lanes {
   unsigned count;
   unsigned component[4];
} xe_texture_lanes;
static INLINE int xe_texture_lanes_init(xe_texture_lanes *out,uint32_t mask)
{
   unsigned i;
   if(!out)return 0;
   out->count=0;
   if(!mask || (mask&~UINT32_C(15)))return 0;
   for(i=0;i<4;++i)if(mask&(UINT32_C(1)<<i))
      out->component[out->count++]=i;
   return 1;
}
#endif
