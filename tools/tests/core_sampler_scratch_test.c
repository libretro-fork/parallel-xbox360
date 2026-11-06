/* C89 ownership, copy-free commits, descriptor publication and growth failures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations, releases;
static int fail_alloc;
static void *test_realloc(void *p, size_t n) { ++allocations; return fail_alloc ? NULL : realloc(p,n); }
static void test_free(void *p) { if(p) ++releases; free(p); }
#define XE_SAMPLER_REALLOC test_realloc
#define XE_SAMPLER_FREE test_free
#include "core_sampler_scratch.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"sampler line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void populate(xe_sampler_stage *s, unsigned seed) {
  xe_sampler_buffer *b=xe_sampler_current(s);
  size_t i;
  for(i=0;i<b->count;++i) {
    b->data[i].parameters=(uint32_t)i*37+seed;
    b->data[i].sampler=((uint64_t)1<<48)+(uint64_t)i*101+seed;
  }
}
#ifdef XE_SAMPLER_BRIDGE
extern int xe_test_sampler_reference(void);
#endif
int main(void) {
  xe_sampler_stage s={{ {NULL,0,0},{NULL,0,0} },0,0};
  xe_sampler_buffer *current;
  xe_sampler_record saved[80], *first;
  unsigned before, i, written;
  CHECK(xe_sampler_begin(&s,0) && xe_sampler_matches(&s) && !allocations);
  CHECK(!xe_sampler_descriptor_count_valid(&s,3) && xe_sampler_descriptor_count_valid(&s,0));
  CHECK(xe_sampler_begin(&s,80));
  memset(xe_sampler_current(&s)->data,0xa7,80*sizeof(xe_sampler_record)); populate(&s,7);
  CHECK(!xe_sampler_matches(&s) && xe_sampler_descriptor_count_valid(&s,80));
  current=xe_sampler_current(&s); first=current->data; memcpy(saved,first,sizeof(saved));
  before=allocations; xe_sampler_commit(&s); xe_sampler_commit(&s);
  CHECK(xe_sampler_current(&s)->data==first && xe_sampler_matches(&s) && allocations==before);
  CHECK(!memcmp(saved,first,sizeof(saved)));
  CHECK(xe_sampler_begin(&s,80)); populate(&s,7);
  CHECK(xe_sampler_current(&s)->data!=first && xe_sampler_matches(&s));
  xe_sampler_current(&s)->data[79].sampler^=1;
  CHECK(!xe_sampler_matches(&s)); xe_sampler_current(&s)->data[79].sampler^=1;
  xe_sampler_current(&s)->data[79].parameters^=1; CHECK(!xe_sampler_matches(&s));
  CHECK(!memcmp(saved,first,sizeof(saved)));
  written=s.written; fail_alloc=1; before=allocations;
  CHECK(!xe_sampler_begin(&s,1000) && s.written==written && allocations==before+1);
  CHECK(s.buffers[written].data==first && !memcmp(saved,first,sizeof(saved)));
  fail_alloc=0;
  CHECK(!xe_sampler_begin(&s,(size_t)-1) && s.written==written);
  CHECK(xe_sampler_begin(&s,80)); populate(&s,7); xe_sampler_commit(&s);
  /* Alternate both owned slots after warm-up. Retained written bytes must not
   * be overwritten by prepare or retries; commits never allocate or copy them. */
  before=allocations;
  for(i=0;i<10000;++i) {
    CHECK(xe_sampler_begin(&s,80)); populate(&s,7);
    CHECK(xe_sampler_matches(&s));
    first=xe_sampler_current(&s)->data;
    xe_sampler_commit(&s);
    CHECK(xe_sampler_current(&s)->data==first);
  }
  CHECK(allocations==before);
  CHECK(xe_sampler_begin(&s,0) && !xe_sampler_matches(&s));
  xe_sampler_commit(&s); CHECK(xe_sampler_matches(&s));
  xe_sampler_stage_free(&s); xe_sampler_stage_free(&s);
  CHECK(!s.buffers[0].data && !s.buffers[1].data && releases==2);
#ifdef XE_SAMPLER_BRIDGE
  CHECK(xe_test_sampler_reference());
#endif
  puts("Sampler scratch: rotating ownership, stable retries, publication counts and allocation failures pass");
  return 0;
}
