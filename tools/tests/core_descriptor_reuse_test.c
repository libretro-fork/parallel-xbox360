/* C89 frame-gated stable descriptor records and retained reset capacity. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations,releases;
static int fail_alloc;
static void *allocate(size_t n) { ++allocations; return fail_alloc ? NULL : malloc(n); }
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
  CHECK(allocations==before); first=pool.head;
  CHECK(!xe_descriptor_reuse_acquire(&pool,9));
  CHECK(!xe_descriptor_reuse_collect(&pool,9));
  fail_alloc=1;
  CHECK(!xe_descriptor_reuse_acquire(&pool,10) && !pool.leased && pool.head==first);
  CHECK(first->handle==1 && first->frame==10);
  fail_alloc=0;
  node=xe_descriptor_reuse_acquire(&pool,11); CHECK(node && !node->handle);
  CHECK(!xe_descriptor_reuse_commit(&pool)); node->handle=129;
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(pool.head==first && first->handle==1);
  CHECK(xe_descriptor_reuse_collect(&pool,10)==128);
  node=xe_descriptor_reuse_acquire(&pool,12); CHECK(node && node->handle==128);
  CHECK(!xe_descriptor_reuse_reset(&pool) && pool.leased==node);
  CHECK(!xe_descriptor_reuse_acquire(&pool,12));
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(xe_descriptor_reuse_collect(&pool,11)==1);
  node=xe_descriptor_reuse_acquire(&pool,12); CHECK(node && node->handle==129);
  xe_descriptor_reuse_cancel(&pool);
  CHECK(xe_descriptor_reuse_acquire(&pool,12)==node && node->handle==129);
  CHECK(xe_descriptor_reuse_commit(&pool));
  CHECK(xe_descriptor_reuse_collect(&pool,12)==2);
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
    CHECK(xe_descriptor_reuse_collect(&pool,frame)==128);
    if(!(frame%1000)) CHECK(xe_descriptor_reuse_reset(&pool));
  }
  CHECK(allocations==before && native_allocations==1280);
  xe_descriptor_reuse_destroy(&pool); xe_descriptor_reuse_destroy(&pool);
  CHECK(!pool.pages && !pool.head && !pool.free && releases==2);
#ifdef XE_REUSE_BRIDGE
  CHECK(xe_test_descriptor_reuse_reference());
#endif
  puts("Descriptor reuse: 1.28M frame-gated leases, LIFO order, growth failure and retained reset capacity pass");
  return 0;
}
