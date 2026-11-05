#include <stdio.h>
#include <stdlib.h>
#include "libretro/core_gpu_downscale.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %u\n",(unsigned)__LINE__); exit(1); } } while (0)
int main(void)
{
   xe_gpu_downscale_view view;
   uint64_t source, destination, total_source, total_destination;
   uint32_t pixel, area, alignment, remaining, original, passes = 0;
   for (pixel=0;pixel<4;++pixel) for (area=1;area<=64;++area)
   for (alignment=4;alignment<=4096;alignment*=2) {
      source = ((uint64_t)6<<30) + 64;
      destination = 128;
      total_source = source;
      total_destination = destination;
      remaining = original = 10003;
      while (remaining) {
         CHECK(xe_gpu_downscale_plan(source,destination,(uint64_t)64<<30,
             (uint64_t)1<<29,1024u<<pixel,area,remaining,alignment,
             1024u*1024u,17,&view));
         CHECK(view.tiles && view.tiles<=17 && view.tiles<=remaining);
         CHECK(!(view.source_offset & (alignment-1)));
         CHECK(!(view.destination_offset & (alignment-1)));
         CHECK(view.source_offset+view.source_bias==source);
         CHECK(view.destination_offset+view.destination_bias==destination);
         CHECK(view.source_size<=1024u*1024u && view.destination_size<=1024u*1024u);
         CHECK(view.source_size-view.source_bias==(1024u<<pixel)*area*view.tiles);
         CHECK(view.destination_size-view.destination_bias==(1024u<<pixel)*view.tiles);
         source += (uint64_t)(1024u<<pixel)*area*view.tiles;
         destination += (uint64_t)(1024u<<pixel)*view.tiles;
         remaining -= view.tiles;
         ++passes;
      }
      CHECK(source-total_source==(uint64_t)(1024u<<pixel)*area*original);
      CHECK(destination-total_destination==(uint64_t)(1024u<<pixel)*original);
   }
   CHECK(!xe_gpu_downscale_plan(0,0,1,1,1024,4,1,4,65536,1,&view));
   CHECK(!xe_gpu_downscale_plan(0,0,65536,65536,1024,4,1,3,65536,1,&view));
   CHECK(!xe_gpu_downscale_plan(1,0,65536,65536,1024,4,1,4,65536,1,&view));
   CHECK(!xe_gpu_downscale_plan(0,0,65536,65536,1024,4,1,4,65536,0,&view));
   CHECK(!xe_gpu_downscale_plan(UINT64_MAX,0,UINT64_MAX,65536,1024,4,1,4,65536,1,&view));
   CHECK(!xe_gpu_downscale_plan(0,0,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,4,UINT32_MAX,1,&view));
   CHECK(xe_gpu_downscale_plan(0,0,4096,1024,1024,4,1,4,4096,1,&view));
   printf("PASS: %u bounded borrowed-view chunks, 8/16/32/64bpp, scale areas 1-64, >4GiB offsets, range/group limits and overflow rejection\n",passes);
   return 0;
}
