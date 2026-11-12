/* Owned primitive restart metadata, single CPU owner. MSVC C89.
 * Copyright 2021 Ben Vanik. All rights reserved.
 * Range scanning adapted from Xenia primitive_processor.cc; BSD, see LICENSE. */
#ifndef XE_CORE_PRIMITIVE_RANGES_H
#define XE_CORE_PRIMITIVE_RANGES_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#include "core_primitive_builtin.h"
#define XE_PRIMITIVE_FIXED_RANGES 1

/* VGT_DRAW_INITIATOR has at most65535 indices; DMA_SIZE only clamps it down.
 * The smallest emitting primitive is a two-vertex line loop. Including reset
 * separators, at most floor((65535+1)/3) positive-output segments can exist.
 * Other supported topologies require at least three vertices per segment. */
#define XE_PRIMITIVE_RANGES_MAX 21845u
typedef struct xe_primitive_range {
   uint32_t guest_offset,guest_index_count,host_index_count;
} xe_primitive_range;
typedef char xe_primitive_range_12_bytes[sizeof(xe_primitive_range)==12?1:-1];
typedef struct xe_primitive_ranges {
   xe_primitive_range *data;
   uint32_t count,capacity;
} xe_primitive_ranges;

#ifndef XE_PRIMITIVE_RANGES_ALLOC
#define XE_PRIMITIVE_RANGES_ALLOC malloc
#endif
#ifndef XE_PRIMITIVE_RANGES_FREE
#define XE_PRIMITIVE_RANGES_FREE free
#endif

/* Start from an all-zero owner; never copy an initialized owner. Allocate only
 * at processor initialization. Cold writes touch each4KiB of the allocation;
 * no per-draw payload clear or resize follows. A failed allocation changes no
 * owner fields. Clear/close require all borrowed CPU row views to be finished. */
static INLINE int xe_primitive_ranges_init(xe_primitive_ranges *ranges)
{
   xe_primitive_range *data;
   volatile unsigned char *touch;
   size_t i,bytes=XE_PRIMITIVE_RANGES_MAX*sizeof(xe_primitive_range);
   if(!ranges || ranges->data || ranges->count || ranges->capacity)return 0;
   data=(xe_primitive_range*)XE_PRIMITIVE_RANGES_ALLOC(bytes);
   if(!data)return 0;
   touch=(volatile unsigned char*)data;
   for(i=0;i<bytes;i+=4096)touch[i]=0;
   touch[bytes-1]=0;
   ranges->data=data;ranges->count=0;ranges->capacity=XE_PRIMITIVE_RANGES_MAX;
   return 1;
}
static INLINE void xe_primitive_ranges_clear(xe_primitive_ranges *ranges)
{
   if(ranges)ranges->count=0;
}
static INLINE void xe_primitive_ranges_close(xe_primitive_ranges *ranges)
{
   if(!ranges)return;
   if(ranges->data)XE_PRIMITIVE_RANGES_FREE(ranges->data);
   ranges->data=NULL;ranges->count=0;ranges->capacity=0;
}
static INLINE int xe_primitive_ranges_push(xe_primitive_ranges *ranges,
      uint32_t offset,uint32_t guest_count,uint32_t host_count)
{
   xe_primitive_range *row;
   if(!ranges || !ranges->data || ranges->count>=ranges->capacity ||
      ranges->capacity>XE_PRIMITIVE_RANGES_MAX || offset>65535 ||
      guest_count>65535-offset)return 0;
   row=ranges->data+ranges->count;
   row->guest_offset=offset;row->guest_index_count=guest_count;
   row->host_index_count=host_count;++ranges->count;
   return 1;
}

/* Keep the hot scans typed: no width branch or widened reset comparison for
 * every short segment. Both kernels publish the same checked final rows. */
#define XE_PRIMITIVE_RANGE_SCAN_BODY(VALUE) \
   uint32_t written=ranges->count,capacity=ranges->capacity; \
   uint32_t total=0,at=0,begin,guest,host; \
   uint32_t minimum=counter(2)?2:counter(3)?3:4; \
   xe_primitive_range *data=ranges->data,*row; \
   for(;;) { \
      begin=at; \
      while(at<count && (VALUE)!=reset)++at; \
      guest=at-begin;host=guest<minimum?0:counter(guest); \
      if(host) { \
         if(host>=UINT32_MAX-total || written>=capacity)return UINT32_MAX; \
         row=data+written++; \
         row->guest_offset=begin;row->guest_index_count=guest; \
         row->host_index_count=host;total+=host; \
      } \
      if(at>=count)break; \
      ++at; \
   } \
   ranges->count=written;return total;

static INLINE uint32_t xe_primitive_ranges_scan16(xe_primitive_ranges *ranges,
      xe_primitive_index_count counter,const uint16_t *source,uint32_t count,
      uint16_t reset)
{
   XE_PRIMITIVE_RANGE_SCAN_BODY(source[at])
}
static INLINE uint32_t xe_primitive_ranges_scan32(xe_primitive_ranges *ranges,
      xe_primitive_index_count counter,const uint32_t *source,uint32_t count,
      uint32_t reset,uint32_t mask)
{
   XE_PRIMITIVE_RANGE_SCAN_BODY(source[at]&mask)
}
#undef XE_PRIMITIVE_RANGE_SCAN_BODY

/* Append positive-output segments directly to the final row storage. The
 * count callback is borrowed for this call and is a pure supported topology count
 * function (zero for fewer than two vertices). Probe its minimum emitting
 * length once, so dense reset streams do not call it for discarded segments.
 * Input is aligned to its index
 * width, immutable and disjoint from output. Source payload is never copied.
 * The published prefix remains intact on failure; unpublished tail rows may
 * change. UINT32_MAX signals invalid bounds/capacity/count overflow. The draw
 * owner consumes the retained segmentation before clear; do not replace this
 * snapshot with a second scan of possibly changing guest reset markers. */
static INLINE uint32_t xe_primitive_ranges_append(xe_primitive_ranges *ranges,
      xe_primitive_index_count counter,const void *source,uint32_t count,
      unsigned width,uint32_t reset,uint32_t mask)
{
   if(!ranges || !counter || count>65535 || (width!=2 && width!=4))return UINT32_MAX;
   if(width==2 && reset>UINT16_MAX)return UINT32_MAX;
   if(count && !source)return UINT32_MAX;
   if(count && ((uintptr_t)source&(width-1)))return UINT32_MAX;
   if(ranges->count>ranges->capacity || ranges->capacity>XE_PRIMITIVE_RANGES_MAX ||
      (ranges->capacity && !ranges->data))return UINT32_MAX;
   /* Publish once. Validated input bounds already prove every row's guest
    * offset/count; avoid generic-push checks and owner writes per segment. */
   return width==2?xe_primitive_ranges_scan16(ranges,counter,
         (const uint16_t*)source,count,(uint16_t)reset):
         xe_primitive_ranges_scan32(ranges,counter,
         (const uint32_t*)source,count,reset,mask);
}
#endif
