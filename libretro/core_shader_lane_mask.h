/* Pack a four-lane source mask into the caller's selected-lane order.
 * MSVC C89; no retained storage, allocation or source-record mutation. */
#ifndef XE_CORE_SHADER_LANE_MASK_H
#define XE_CORE_SHADER_LANE_MASK_H
#include "../third_party/libretro-common/include/retro_inline.h"
static INLINE unsigned xe_shader_pack_lane_mask(unsigned selected,unsigned mask)
{
   unsigned source,destination=1,result=0;
   selected&=15u;mask&=selected;
   if(selected==15u)return mask;
   for(source=1;source!=16;source<<=1) {
      if(selected&source) {
         if(mask&source)result|=destination;
         destination<<=1;
      }
   }
   return result;
}
#endif
