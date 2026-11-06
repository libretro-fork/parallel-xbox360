/* C89 frame-gated stable descriptor records and retained reset capacity. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations,releases;
static unsigned successful, fail_at;
static int fail_alloc;
static void *allocate(size_t n) {
  void *p;
  ++allocations; if(fail_alloc || allocations==fail_at) return NULL;
  p=malloc(n); if(p) ++successful; return p;
}
static void release(void *p) { if(p) ++releases; free(p); }
#define XE_DESCRIPTOR_REUSE_ALLOC allocate
#define XE_DESCRIPTOR_REUSE_FREE release
#include "core_descriptor_reuse.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"reuse line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#ifdef XE_REUSE_BRIDGE
extern int xe_test_descriptor_reuse_reference(void);
#endif
int main(void) {
  xe_descriptor_reuse pool={0};
  xe_descriptor_reuse_node *node,*first;
  unsigned i,frame,before,native_allocations=0;
  fail_alloc=1; CHECK(!xe_descriptor_reuse_warm(&pool) && !pool.pages);
  fail_alloc=0; CHECK(xe_descriptor_reuse_warm(&pool)); before=allocations;
  for(i=0;i<XE_DESCRIPTOR_REUSE_PAGE_SLOTS;++i) {
    node=xe_descriptor_reuse_acquire(&pool,10); CHECK(node && !node->handle);
    node->handle=i+1; CHECK(xe_descriptor_reuse_commit(&pool));
  }
  CHECK(allocations==before); first=pool.head->tail;
  CHECK(!xe_descriptor_reuse_acquire(&pool,9));
  CHECK(!xe_descriptor_reuse_collect(&pool,9));
  fail_alloc=1;
  CHECK(!xe_descriptor_reuse_acquire(&pool,11) && !pool.leased && pool.head->tail==first && pool.free_frames);
  CHECK(first->handle==1 && pool.head->frame==10 && pool.head->head->handle==128);
  fail_alloc=0;
  node=xe_descriptor_reuse_acquire(&pool,11); CHECK(node && !node->handle);
  CHECK(!xe_descriptor_reuse_commit(&pool)); node->handle=129;
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(pool.head->tail==first && first->handle==1);
  CHECK(xe_descriptor_reuse_collect(&pool,10)==1);
  node=xe_descriptor_reuse_acquire(&pool,12); CHECK(node && node->handle==128);
  CHECK(!xe_descriptor_reuse_reset(&pool) && pool.leased==node);
  CHECK(!xe_descriptor_reuse_acquire(&pool,12));
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(xe_descriptor_reuse_collect(&pool,11)==1);
  node=xe_descriptor_reuse_acquire(&pool,12); CHECK(node && node->handle==129);
  xe_descriptor_reuse_cancel(&pool);
  CHECK(xe_descriptor_reuse_acquire(&pool,12)==node && node->handle==129);
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(xe_descriptor_reuse_collect(&pool,12)==1);
  CHECK(xe_descriptor_reuse_reset(&pool));
  node=xe_descriptor_reuse_acquire(&pool,1); CHECK(node==first && !node->handle);
  xe_descriptor_reuse_cancel(&pool);
  CHECK(xe_descriptor_reuse_reset(&pool)); before=allocations;
  for(frame=1;frame<=10000;++frame) {
    for(i=0;i<128;++i) {
      node=xe_descriptor_reuse_acquire(&pool,frame); CHECK(node);
      if(!node->handle) node->handle=++native_allocations;
      CHECK(xe_descriptor_reuse_commit(&pool));
    }
    CHECK(xe_descriptor_reuse_collect(&pool,frame)==1);
    if(!(frame%1000)) CHECK(xe_descriptor_reuse_reset(&pool));
  }
  CHECK(allocations==before && native_allocations==1280);
  xe_descriptor_reuse_destroy(&pool); xe_descriptor_reuse_destroy(&pool);
  CHECK(!pool.pages && !pool.frame_pages && !pool.head && !pool.free && releases==successful);
  /* Partial warm-up owns its successful first page, even if frame storage fails. */
  fail_at=allocations+2;
  CHECK(!xe_descriptor_reuse_warm(&pool) && pool.pages && !pool.frame_pages);
  fail_at=0; CHECK(xe_descriptor_reuse_warm(&pool));
  for(frame=1;frame<=8;++frame) {
    node=xe_descriptor_reuse_acquire(&pool,frame); CHECK(node);
    node->handle=frame; CHECK(xe_descriptor_reuse_commit(&pool));
  }
  fail_alloc=1;
  CHECK(!xe_descriptor_reuse_acquire(&pool,9) && !pool.leased && pool.next_slot==8);
  fail_alloc=0;
  node=xe_descriptor_reuse_acquire(&pool,9); CHECK(node);
  node->handle=9; CHECK(xe_descriptor_reuse_commit(&pool));
  /* A live lease referencing the last pending batch pins that batch, while
   * earlier completed batches may be retired. Cancellation releases the pin. */
  node=xe_descriptor_reuse_acquire(&pool,9); CHECK(node && !node->handle);
  CHECK(xe_descriptor_reuse_collect(&pool,9)==8 && pool.head->frame==9);
  xe_descriptor_reuse_cancel(&pool);
  CHECK(pool.unused && !pool.unused->handle);
  CHECK(xe_descriptor_reuse_collect(&pool,9)==1);
  node=xe_descriptor_reuse_acquire(&pool,10); CHECK(node && node->handle==9);
  CHECK(!xe_descriptor_reuse_collect(&pool,UINT64_MAX));
  CHECK(xe_descriptor_reuse_commit(&pool));
  node=xe_descriptor_reuse_acquire(&pool,11); CHECK(node && node->handle==8);
  CHECK(xe_descriptor_reuse_collect(&pool,10)==1 && !pool.tail);
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(xe_descriptor_reuse_collect(&pool,UINT64_MAX)==1);
  xe_descriptor_reuse_destroy(&pool);
  CHECK(releases==successful);
#ifdef XE_REUSE_BRIDGE
  CHECK(xe_test_descriptor_reuse_reference());
#endif
  puts("Descriptor reuse: 1.28M frame-gated leases, LIFO order, growth failure and retained reset capacity pass");
  return 0;
}
