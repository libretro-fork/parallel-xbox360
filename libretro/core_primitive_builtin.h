/* Built-in primitive indices, written directly to borrowed upload storage.
 * Copyright 2021 Ben Vanik. All rights reserved.
 * MSVC C89 adaptation of Xenia primitive_processor.cc; BSD license, see LICENSE. */
#ifndef XE_CORE_PRIMITIVE_BUILTIN_H
#define XE_CORE_PRIMITIVE_BUILTIN_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_primitive_builtin_layout {
   size_t offsets[4]; /* two-triangle strips, fans, quads, triangle strips */
   uint32_t two_triangle_count;
} xe_primitive_builtin_layout;
typedef uint32_t (*xe_primitive_index_count)(uint32_t);

/* The layout stays immutable and disjoint from output through this call.
 * Disabled ranges have SIZE_MAX offsets. Validate every range before writing;
 * no output modification on invalid layouts or an unaligned/null mapping. */
static INLINE int xe_primitive_builtin_fill(void *mapping,size_t size,
      const xe_primitive_builtin_layout *layout)
{
   size_t lengths[4],a,c,offset;
   uint32_t i,j;
   uint32_t *wide;
   uint16_t *narrow;
   if(!mapping || !layout || ((uintptr_t)mapping&3) ||
      layout->two_triangle_count>65535) return 0;
   lengths[0]=layout->two_triangle_count?
         ((size_t)layout->two_triangle_count*5-1)*4:0;
   lengths[1]=(size_t)(65535-2)*3*2;
   lengths[2]=(size_t)(65535/4)*6*2;
   lengths[3]=lengths[1];
   for(a=0;a<4;++a) {
      offset=layout->offsets[a];
      if(offset==SIZE_MAX) continue;
      if((offset&(a?1:3)) || offset>size || lengths[a]>size-offset) return 0;
      for(c=0;c<a;++c) if(layout->offsets[c]!=SIZE_MAX && lengths[a] && lengths[c] &&
            offset<layout->offsets[c]+lengths[c] && layout->offsets[c]<offset+lengths[a]) return 0;
   }
   if(layout->offsets[0]!=SIZE_MAX && layout->two_triangle_count) {
      wide=(uint32_t*)((unsigned char*)mapping+layout->offsets[0]);
      for(i=0;i<layout->two_triangle_count;++i) {
         if(i) *wide++=UINT32_MAX;
         for(j=0;j<4;++j) *wide++=i*4+j;
      }
   }
   if(layout->offsets[1]!=SIZE_MAX) {
      narrow=(uint16_t*)((unsigned char*)mapping+layout->offsets[1]);
      for(i=2;i<65535;++i) { *narrow++=(uint16_t)(i-1);*narrow++=(uint16_t)i;*narrow++=0; }
   }
   if(layout->offsets[2]!=SIZE_MAX) {
      narrow=(uint16_t*)((unsigned char*)mapping+layout->offsets[2]);
      for(i=0;i<65535/4;++i) {
         *narrow++=(uint16_t)(i*4);*narrow++=(uint16_t)(i*4+1);*narrow++=(uint16_t)(i*4+2);
         *narrow++=(uint16_t)(i*4);*narrow++=(uint16_t)(i*4+2);*narrow++=(uint16_t)(i*4+3);
      }
   }
   if(layout->offsets[3]!=SIZE_MAX) {
      narrow=(uint16_t*)((unsigned char*)mapping+layout->offsets[3]);
      for(i=2;i<65535;++i) {
         *narrow++=(uint16_t)(i-2+(i&1));*narrow++=(uint16_t)(i-1-(i&1));*narrow++=(uint16_t)i;
      }
   }
   return 1;
}
#endif
