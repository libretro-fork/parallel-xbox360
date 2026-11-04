/* C89: submitted GPU references stay alive; dependency order and shutdown. */
#include "libretro/core_gpu_retire.h"
#include "libretro/core_gpu_resolve.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
struct resource {
   xe_gpu_retire_node node;
   unsigned order;
};
static unsigned released;
static void release_handle(void *opaque, uint64_t handle)
{
   unsigned *next = (unsigned *)opaque;
   CHECK(handle == *next);
   ++*next;
}
static void test_fixed_pool(void)
{
   xe_gpu_retire_queue queue = {0};
   xe_gpu_retire_pool pool = {0};
   xe_gpu_retire_slot slots[2];
   xe_gpu_retire_budget budget = {0};
   unsigned next = 10;
   CHECK(!xe_gpu_retire_pool_init(NULL,slots,2));
   CHECK(xe_gpu_retire_pool_init(&pool,slots,2));
   CHECK(!xe_gpu_retire_handle(NULL,&pool,7,10,release_handle,&next));
   CHECK(pool.available == 2);
   CHECK(xe_gpu_retire_handle(&queue,&pool,7,10,release_handle,&next));
   CHECK(!xe_gpu_retire_handle(&queue,&pool,6,11,release_handle,&next));
   CHECK(pool.available == 1);
   CHECK(xe_gpu_retire_handle(&queue,&pool,7,11,release_handle,&next));
   CHECK(!xe_gpu_retire_handle(&queue,&pool,7,12,release_handle,&next));
   CHECK(!xe_gpu_retire_collect_frame(&queue,6,&budget,1,1));
   CHECK(xe_gpu_retire_collect_frame(&queue,7,&budget,1,1) == 1);
   CHECK(pool.available == 1 && next == 11);
   CHECK(!xe_gpu_retire_collect_frame(&queue,7,&budget,1,1));
   CHECK(xe_gpu_retire_collect_frame(&queue,7,&budget,2,1) == 1);
   CHECK(pool.available == 2 && next == 12);
   CHECK(xe_gpu_retire_handle(&queue,&pool,8,12,release_handle,&next));
   queue.head->cost = 8;
   CHECK(xe_gpu_retire_handle(&queue,&pool,8,13,release_handle,&next));
   CHECK(xe_gpu_retire_collect_frame(&queue,8,&budget,3,2) == 1);
   CHECK(!xe_gpu_retire_collect_frame(&queue,8,&budget,3,2));
   CHECK(xe_gpu_retire_collect(&queue,UINT64_MAX) == 1);
   CHECK(pool.available == 2 && next == 14);
}
static void test_scaled_views(void)
{
   uint64_t offset, size;
   uint64_t a, b;
   uint32_t i;
   const uint64_t capacity = (uint64_t)2 << 30;
   CHECK(!xe_gpu_resolve_range(0,1,0,0,capacity,&offset,&size));
   CHECK(!xe_gpu_resolve_range(0,1,4,64,capacity,&offset,&size));
   CHECK(!xe_gpu_resolve_range(UINT32_MAX,1,4,0,capacity,&offset,&size));
   CHECK(!xe_gpu_resolve_range((UINT32_C(1)<<29)-1,2,4,0,capacity,&offset,&size));
   CHECK(!xe_gpu_resolve_range((UINT32_C(1)<<29)-1,1,4,4,capacity,&offset,&size));
   CHECK(!xe_gpu_resolve_range(0,4,4,0,15,&offset,&size));
   CHECK(xe_gpu_resolve_range(0,UINT32_C(1)<<29,4,0,capacity,&offset,&size));
   CHECK(!offset && size == capacity);
   /* More than 32 requests and overlapping views all retain one address
    * interpretation. No reassignment of a backing buffer is permitted. */
   for (i=0;i<20000;++i) {
      CHECK(xe_gpu_resolve_range(i*4096,8192,4,4,capacity,&offset,&size));
      a=offset+4096*4;
      CHECK(xe_gpu_resolve_range(i*4096+4096,4096,4,4,capacity,&offset,&size));
      b=offset;
      CHECK(a == b && size == 4096*4);
   }
}
static void release(void *opaque)
{
   struct resource *resource = (struct resource*)opaque;
   CHECK(resource->order == released);
   ++released;
   free(resource);
}
static void enqueue(xe_gpu_retire_queue *queue, unsigned order, uint64_t token)
{
   struct resource *resource = (struct resource*)calloc(1,sizeof(*resource));
   CHECK(resource);
   resource->order = order;
   CHECK(xe_gpu_retire_append(queue,&resource->node,token,release,resource));
}
int main(void)
{
   xe_gpu_retire_queue queue = {0};
   xe_gpu_cache_gc gc = {0};
   xe_gpu_retire_node invalid = {0};
   unsigned frame, expected;
   xe_gpu_surface_choice choice = {0};
   int a, b;
   test_fixed_pool();
   test_scaled_views();
   CHECK(xe_gpu_surface_recent(2, UINT64_MAX, 4));
   CHECK(xe_gpu_surface_recent(UINT64_MAX, UINT64_MAX-3, 4));
   CHECK(!xe_gpu_surface_recent(UINT64_MAX, UINT64_MAX-4, 4));
   xe_gpu_surface_consider(&choice, &a, 7, 8, 10, 0, 0);
   xe_gpu_surface_consider(&choice, &a, 7, 7, 10, 0, 1);
   xe_gpu_surface_consider(&choice, &a, 7, 7, 10, 7, 0);
   CHECK(!choice.resource);
   xe_gpu_surface_consider(&choice, &a, 7, 7, 10, 6, 0);
   CHECK(choice.resource == &a);
   xe_gpu_surface_consider(&choice, &b, 7, 7, 10, 5, 0);
   CHECK(choice.resource == &b && choice.last_use == 5);
   CHECK(!xe_gpu_retire_append(NULL,&invalid,0,release,NULL));
   CHECK(!xe_gpu_retire_append(&queue,NULL,0,release,NULL));
   CHECK(!xe_gpu_retire_append(&queue,&invalid,0,NULL,NULL));
   CHECK(!xe_gpu_retire_collect(NULL,UINT64_MAX));
   /* A framebuffer batch must be released before the images it references. */
   enqueue(&queue,0,7);
   enqueue(&queue,1,7);
   enqueue(&queue,2,11);
   CHECK(!xe_gpu_retire_collect(&queue,6));
   CHECK(released == 0);
   CHECK(!xe_gpu_retire_append(&queue,&invalid,10,release,NULL));
   CHECK(xe_gpu_retire_collect(&queue,7) == 2);
   CHECK(released == 2 && queue.head == queue.tail);
   CHECK(!xe_gpu_retire_collect(&queue,10));
   CHECK(xe_gpu_retire_collect(&queue,11) == 1);
   CHECK(!queue.head && !queue.tail);
   /* Keep four submissions in flight while retiring dependency pairs. The
    * callbacks free their containing node, exposing post-release access to ASan. */
   for (frame = 1; frame <= 100000; ++frame) {
      enqueue(&queue,released + (frame > 4 ? 8 : (frame-1)*2),frame+11);
      enqueue(&queue,released + (frame > 4 ? 8 : (frame-1)*2) + 1,frame+11);
      expected = frame > 4 ? 2 : 0;
      CHECK(xe_gpu_retire_collect(&queue,frame > 4 ? frame+7 : 11) == expected);
   }
   CHECK(xe_gpu_retire_collect(&queue,UINT64_MAX) == 8);
   CHECK(released == 200003);
   enqueue(&queue,released,UINT64_MAX);
   CHECK(!xe_gpu_retire_collect(&queue,UINT64_MAX-1));
   CHECK(xe_gpu_retire_collect(&queue,UINT64_MAX) == 1);
   CHECK(!xe_gpu_cache_gc_due(&gc,0,512,512,1));
   CHECK(xe_gpu_cache_gc_due(&gc,0,513,512,1));
   xe_gpu_cache_gc_commit(&gc,0,1,120);
   CHECK(!xe_gpu_cache_gc_due(&gc,119,513,512,2));
   CHECK(!xe_gpu_cache_gc_due(&gc,120,513,512,1));
   CHECK(xe_gpu_cache_gc_due(&gc,120,513,512,2));
   xe_gpu_cache_gc_commit(&gc,UINT64_MAX-1,2,120);
   CHECK(gc.next_frame == UINT64_MAX);
   CHECK(!xe_gpu_cache_gc_due(&gc,UINT64_MAX-1,513,512,3));
   CHECK(xe_gpu_cache_gc_due(&gc,UINT64_MAX,513,512,3));
   puts("PASS: GPU lifetime/order, 200000 retirements, fixed pool/backpressure, per-frame release budget, surface reuse and overlapping scaled views");
   return 0;
}
