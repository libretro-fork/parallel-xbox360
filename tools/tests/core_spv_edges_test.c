/* Actual C89 arena, independent ordered pointer oracle and fault injection. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations, frees, fail_after=(size_t)-1;
static void *allocate(size_t n) {void *p;if(allocations==fail_after)return NULL;p=malloc(n);if(p)++allocations;return p;}
static void release(void *p){if(p)++frees;free(p);}
#define XE_SPV_ARENA_ALLOC allocate
#define XE_SPV_ARENA_FREE release
#include "../../libretro/core_spv_arena.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"edges C89 line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned targets[4096];
static xe_spv_edges lists[64];
int main(int argc,char **argv){
  xe_spv_arena arena={0},fault={0};
  xe_spv_edges a={0},b={0},over={0};
  xe_spv_record_view prefix,view;
  void **old;
  size_t i,j,round,cold,count;
  CHECK(!xe_spv_arena_edges_push(&arena,&a,targets));
  CHECK(xe_spv_arena_begin(&arena));fail_after=allocations;
  CHECK(!xe_spv_arena_edges_push(&arena,&a,targets));CHECK(!a.records&&!a.count&&!a.capacity);
  fail_after=(size_t)-1;
  CHECK(xe_spv_arena_edges_push(&arena,&a,targets));
  CHECK(xe_spv_arena_edges_push(&arena,&a,targets));old=a.records;
  CHECK(xe_spv_arena_edges_push(&arena,&a,targets+1));CHECK(old==a.records); /* Latest grows in place. */
  prefix=xe_spv_edges_view(&a);CHECK(prefix.count==3&&prefix.records[0]==prefix.records[1]);
  CHECK(xe_spv_arena_edges_push(&arena,&b,targets+10));
  CHECK(xe_spv_arena_edges_push(&arena,&a,targets+2));old=a.records;
  CHECK(xe_spv_arena_edges_push(&arena,&a,targets+3));CHECK(old!=a.records); /* Interleaved allocation. */
  if(argc>1&&!strcmp(argv[1],"relocate-uaf"))return ((volatile unsigned char *)old)[0];
  view=xe_spv_edges_view(&a);CHECK(view.count==5);
  CHECK(view.records[0]==targets&&view.records[1]==targets);
  for(i=2;i<5;++i)CHECK(view.records[i]==targets+i-1);
  CHECK(b.records[0]==targets+10&&b.count==1);
  old=a.records;xe_spv_arena_edges_clear(&a);
  if(argc>1&&!strcmp(argv[1],"clear-uaf"))return ((volatile unsigned char *)old)[0];
  CHECK(!xe_spv_edges_view(&a).count&&!a.records&&!a.capacity);
  xe_spv_arena_edges_clear(&b);CHECK(xe_spv_arena_finish(&arena,NULL));
  /* All 64 lists repeatedly reuse warm payload pages; preserve exact duplicates. */
  cold=0;
  for(round=0;round<200;++round){
    CHECK(xe_spv_arena_begin(&arena));
    for(i=0;i<64;++i)for(j=0;j<257;++j)
      CHECK(xe_spv_arena_edges_push(&arena,lists+i,targets+(i+j/2)%4096));
    for(i=0;i<64;++i){view=xe_spv_edges_view(lists+i);CHECK(view.count==257);
      for(j=0;j<view.count;++j)CHECK(view.records[j]==targets+(i+j/2)%4096);
      xe_spv_arena_edges_clear(lists+i);
    }
    if(!round)cold=allocations;else CHECK(allocations==cold);
    CHECK(xe_spv_arena_finish(&arena,NULL));
  }
  CHECK(xe_spv_arena_close(&arena));
  /* A failed relocating growth leaves the old ordered view fully usable. */
  CHECK(xe_spv_arena_begin(&fault));
  CHECK(xe_spv_arena_edges_push(&fault,&a,targets));CHECK(xe_spv_arena_edges_push(&fault,&a,targets+1));
  CHECK(xe_spv_arena_payload_realloc(&fault,NULL,0,16368));
  prefix=xe_spv_edges_view(&a);old=a.records;count=a.capacity;fail_after=allocations;
  CHECK(!xe_spv_arena_edges_push(&fault,&a,targets+2));
  CHECK(a.records==old&&a.count==2&&a.capacity==count&&prefix.records[1]==targets+1);
  fail_after=(size_t)-1;CHECK(xe_spv_arena_edges_push(&fault,&a,targets+2));
  CHECK(a.count==3&&a.records[2]==targets+2);
  CHECK(!xe_spv_arena_edges_push(&fault,&a,NULL));CHECK(a.count==3);
  over.count=over.capacity=(size_t)-1/sizeof(void *);
  CHECK(!xe_spv_arena_edges_push(&fault,&over,targets));CHECK(!over.records);
  xe_spv_arena_edges_clear(&a);CHECK(xe_spv_arena_finish(&fault,NULL));
  CHECK(xe_spv_arena_close(&fault));CHECK(allocations==frees);
  puts("CFG edges C89: ordered/duplicate views, in-place and relocating growth, fault rollback, overflow, retirement and 3,289,600 warm appends pass");return 0;
}
