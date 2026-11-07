/* Evaluate the shared actual emitter against native CPU Xenos addressing. */
#include "core_resolve_tiled_address.h"
#include "core_resolve_pipeline_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
uint32_t xe_test_native_address(unsigned,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,unsigned);
typedef struct trace {
  uint32_t values[512];
  unsigned count, calls, fail_at, instructions;
} trace;
static uintptr_t emit(void *opaque, enum xe_resolve_address_op op, uintptr_t a, uintptr_t b) {
  trace *t = (trace *)opaque;
  uint32_t av, bv, value = 0;
  if (++t->calls == t->fail_at) return 0;
  CHECK(t->count < 512);
  if (op == XE_RESOLVE_ADDR_CONST) value = (uint32_t)a;
  else {
    CHECK(a && b && a < t->count && b < t->count); ++t->instructions;
    av = t->values[a]; bv = t->values[b];
    switch (op) {
      case XE_RESOLVE_ADDR_ADD: value = av + bv; break;
      case XE_RESOLVE_ADDR_MUL: value = av * bv; break;
      case XE_RESOLVE_ADDR_SHL: CHECK(bv < 32); value = av << bv; break;
      case XE_RESOLVE_ADDR_SHR: CHECK(bv < 32); value = av >> bv; break;
      case XE_RESOLVE_ADDR_AND: value = av & bv; break;
      case XE_RESOLVE_ADDR_OR: value = av | bv; break;
      case XE_RESOLVE_ADDR_XOR: value = av ^ bv; break;
      default: CHECK(0);
    }
  }
  t->values[t->count] = value; return t->count++;
}
static unsigned calls[2], instructions[2], cases;
static void check(unsigned volume, uint32_t x, uint32_t y, uint32_t pitch,
    uint32_t slice, uint32_t height, unsigned bpp) {
  trace t;
  uintptr_t result;
  memset(&t,0,sizeof(t)); t.count = 6;
  t.values[1]=x; t.values[2]=y; t.values[3]=pitch; t.values[4]=slice; t.values[5]=height;
  result = xe_resolve_tiled_address(emit,&t,(int)volume,1,2,3,volume?4:0,volume?5:0,bpp);
  CHECK(result && t.values[result] == xe_test_native_address(volume,x,y,pitch,slice,height,bpp));
  if (calls[volume]) CHECK(calls[volume] == t.calls && instructions[volume] == t.instructions);
  calls[volume]=t.calls; instructions[volume]=t.instructions; ++cases;
}
static uint32_t random_word(uint32_t *state) {
  uint32_t x=*state; x ^= x<<13; x ^= x>>17; x ^= x<<5; return *state=x;
}
static void cache_test(void) {
  xe_resolve_pipeline_cache cache;
  uint64_t handle, expected;
  uint32_t key, i, cursor, seen = 0, released = 0;
  memset(&cache,0xa5,sizeof(cache)); xe_resolve_pipeline_reset(&cache);
  for (key = 0; key < XE_RESOLVE_PIPELINE_CAPACITY; ++key) {
    handle = UINT64_MAX;
    CHECK(!xe_resolve_pipeline_get(&cache,key,&handle) && handle == UINT64_MAX);
  }
  for (i = 0; i < XE_RESOLVE_PIPELINE_CAPACITY; ++i) {
    key = (i * 73) & (XE_RESOLVE_PIPELINE_CAPACITY - 1);
    expected = key % 9 ? UINT64_C(0x1122334400000000) | key : 0;
    CHECK(xe_resolve_pipeline_put(&cache,key,expected));
    CHECK(!xe_resolve_pipeline_put(&cache,key,UINT64_MAX));
    CHECK(xe_resolve_pipeline_get(&cache,key,&handle) && handle == expected);
  }
  CHECK(!xe_resolve_pipeline_put(&cache,XE_RESOLVE_PIPELINE_CAPACITY,1));
  CHECK(!xe_resolve_pipeline_put(&cache,UINT32_MAX,1));
  CHECK(!xe_resolve_pipeline_get(&cache,0,NULL));
  cursor = 0;
  while (xe_resolve_pipeline_next(&cache,&cursor,&handle)) {
    key = cursor - 1;
    CHECK(key == seen++);
    expected = key % 9 ? UINT64_C(0x1122334400000000) | key : 0;
    CHECK(handle == expected); if (handle) ++released;
  }
  CHECK(seen == XE_RESOLVE_PIPELINE_CAPACITY && released == 1820);
  xe_resolve_pipeline_reset(&cache);
  CHECK(cache.handles[1] == UINT64_C(0x1122334400000001));
  cursor = 0; CHECK(!xe_resolve_pipeline_next(&cache,&cursor,&handle));
  CHECK(xe_resolve_pipeline_put(&cache,2047,UINT64_MAX));
  CHECK(xe_resolve_pipeline_get(&cache,2047,&handle) && handle == UINT64_MAX);
  xe_resolve_pipeline_reset(&cache); xe_resolve_pipeline_reset(&cache);
  handle = 123;
  CHECK(!xe_resolve_pipeline_get(&cache,2047,&handle) && handle == 123);
  CHECK(!xe_resolve_pipeline_get(&cache,UINT32_MAX,&handle) && handle == 123);
  cursor = UINT32_MAX; CHECK(!xe_resolve_pipeline_next(&cache,&cursor,&handle));
  printf("PASS: %u fixed pipeline slots, cached failures, duplicate rejection, teardown/reuse; %lu bytes\n",
      XE_RESOLVE_PIPELINE_CAPACITY,(unsigned long)sizeof(cache));
}
int main(void) {
  static const uint32_t edges[] = {0,1,3,7,8,15,16,31,32,63,64,79,80,255,256,1023,1024,8191};
  unsigned volume, i, j, bpp, slice, fail;
  uint32_t state=0x13579bdf, x,y,pitch,height;
  trace t;
  cache_test();
  for (volume=0;volume<2;++volume) for(bpp=0;bpp<5;++bpp)
  for(i=0;i<sizeof(edges)/sizeof(edges[0]);++i)
  for(j=0;j<sizeof(edges)/sizeof(edges[0]);++j) for(slice=0;slice<8;++slice)
    check(volume,edges[i],edges[j],256,slice,256,bpp);
  for(i=0;i<1000000;++i) {
    x=random_word(&state)&8191; y=random_word(&state)&8191;
    pitch=1+(random_word(&state)&511); height=1+(random_word(&state)&511);
    slice=random_word(&state)&7; bpp=random_word(&state)%5;
    check(i&1,x,y,pitch,slice,height,bpp);
  }
  for(volume=0;volume<2;++volume) for(fail=1;fail<=calls[volume];++fail) {
    memset(&t,0,sizeof(t)); t.count=6; t.fail_at=fail;
    CHECK(!xe_resolve_tiled_address(emit,&t,(int)volume,1,2,3,4,5,2));
    CHECK(t.calls==fail);
  }
  CHECK(!xe_resolve_tiled_address(NULL,&t,0,1,2,3,0,0,2));
  CHECK(!xe_resolve_tiled_address(emit,&t,0,0,2,3,0,0,2));
  CHECK(!xe_resolve_tiled_address(emit,&t,1,1,2,3,0,5,2));
  CHECK(!xe_resolve_tiled_address(emit,&t,1,1,2,3,4,0,2));
  CHECK(!xe_resolve_tiled_address(emit,&t,0,1,2,3,0,0,5));
  printf("PASS: %u native Xenos addresses; 2D %u, 3D %u arithmetic instructions; all emission failure boundaries\n",
      cases,instructions[0],instructions[1]);
  return 0;
}
