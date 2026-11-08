/* Scalar execution of C89 SSA stack lowering; no emulator or GPU required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_shader_call.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"call line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
typedef struct Fixture { uint32_t values[128][4],pointers[2]; unsigned count,fail; } Fixture;
static uint32_t node(Fixture *f) {if(++f->count==f->fail || f->count>=128)return 0;return f->count;}
static uint32_t constant(void *p,uint32_t v) {Fixture *f=(Fixture*)p;uint32_t n=node(f);if(n)f->values[n][0]=v;return n;}
static uint32_t load(void *p,uint32_t ptr) {Fixture *f=(Fixture*)p;CHECK(ptr<2);return f->pointers[ptr];}
static int store(void *p,uint32_t v,uint32_t ptr) {Fixture *f=(Fixture*)p;CHECK(ptr<2);if(!v)return 0;f->pointers[ptr]=v;return 1;}
static uint32_t binary(void *p,unsigned op,uint32_t a,uint32_t b) {
 Fixture *f=(Fixture*)p;uint32_t n=node(f),x,y;if(!n || !a || !b)return 0;
 x=f->values[a][0];y=f->values[b][0];
 f->values[n][0]=op==XE_CALL_ADD?x+y:op==XE_CALL_SUB?x-y:op==XE_CALL_LESS?(uint32_t)(x<y):(uint32_t)(x!=y);return n;
}
static uint32_t extract(void *p,uint32_t v,unsigned lane) {Fixture *f=(Fixture*)p;CHECK(lane<4);return v?constant(p,f->values[v][lane]):0;}
static uint32_t vector4(void *p,const uint32_t *v) {Fixture *f=(Fixture*)p;uint32_t n=node(f);unsigned i;if(!n)return 0;for(i=0;i<4;++i){if(!v[i])return 0;f->values[n][i]=f->values[v[i]][0];}return n;}
int main(void) {
 Fixture f;xe_shader_call_ops ops;unsigned round,i;uint32_t id,zero;
 memset(&ops,0,sizeof(ops));ops.context=&f;ops.constant=constant;ops.load=load;ops.store=store;ops.binary=binary;ops.extract=extract;ops.vector4=vector4;
 for(round=0;round<10000;++round) {
  memset(&f,0,sizeof(f));zero=constant(&f,0);f.pointers[0]=f.pointers[1]=zero;
  id=xe_shader_call_guard(&ops,1,0);CHECK(id && !f.values[id][0]);
  for(i=0;i<4;++i) {
   id=xe_shader_call_guard(&ops,1,1);CHECK(id && f.values[id][0]);
   CHECK(xe_shader_call_push(&ops,0,1,100+i));
  }
  id=xe_shader_call_guard(&ops,1,1);CHECK(id && !f.values[id][0]);
  for(i=4;i>0;--i) {
   id=xe_shader_call_guard(&ops,1,0);CHECK(id && f.values[id][0]);
   id=xe_shader_call_pop(&ops,0,1);CHECK(id && f.values[id][0]==99+i);
  }
  id=xe_shader_call_guard(&ops,1,0);CHECK(id && !f.values[id][0]);
  CHECK(!f.values[f.pointers[1]][0]);
 }
 for(i=1;i<=7;++i) {
  memset(&f,0,sizeof(f));zero=constant(&f,0);f.pointers[0]=f.pointers[1]=zero;
  f.fail=f.count+i;CHECK(!xe_shader_call_push(&ops,0,1,123));
 }
 for(i=1;i<=8;++i) {
  memset(&f,0,sizeof(f));zero=constant(&f,0);f.pointers[0]=f.pointers[1]=zero;
  CHECK(xe_shader_call_push(&ops,0,1,123));f.fail=f.count+i;
  CHECK(!xe_shader_call_pop(&ops,0,1));
 }
 puts("PASS: 10000 four-level C89 SSA stack executions, empty/full guards and LIFO return destinations");return 0;
}
