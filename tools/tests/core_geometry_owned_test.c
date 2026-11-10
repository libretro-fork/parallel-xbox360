/* Actual native DXIL payload ownership; no frontend or game. MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_ready_owned.h"
#include "guest_geometry.h"
#include <rthreads/rthreads.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"geometry owned line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static retro_atomic_int_t creates,destroys;
static xe_ready_cache cache;
static void options(uint64_t key,XeDxilGeometryOptions* out){
 memset(out,0,sizeof(*out));out->type=1+(unsigned)(key&3);
 out->interpolators=(key&4)?16:1;out->clip_planes=(key&8)?6:0;
 out->cull_planes=(!(key&8) && (key&16))?6:0;out->vertex_kill_and=(key&16)!=0;
 out->point_size=(key&4)!=0;out->point_coordinates=(key&4)!=0;
 out->point_diameter_word=20;out->point_radius_word=22;
}
static void* create(void* opaque,uint64_t key){XeDxilGeometryOptions o;(void)opaque;retro_atomic_fetch_add_int(&creates,1);
 if(key==UINT64_MAX)return NULL;options(key,&o);return xe_dxil_build_geometry_owned(&o);}
static void dispose(void* p){retro_atomic_fetch_add_int(&destroys,1);xe_dxil_shader_binary_delete((XeDxilShaderBinary*)p);}
static void worker(void* opaque){unsigned key,repeat;XeDxilGeometryOptions o;const XeDxilShaderBinary* binary;XeDxilShaderBinary* reference;(void)opaque;
 for(key=0;key<32;++key){
  binary=(const XeDxilShaderBinary*)xe_ready_owned_get(&cache,key,create,dispose,NULL);CHECK(binary && binary->data && binary->size);
  options(key,&o);reference=xe_dxil_build_geometry_owned(&o);
  if(reference && reference->size==binary->size && memcmp(reference->data,binary->data,binary->size)){
   size_t at;for(at=0;at<binary->size;++at)if(reference->data[at]!=binary->data[at]){fprintf(stderr,"key %u differs at %lu: %02x/%02x (%lu bytes)\n",key,(unsigned long)at,binary->data[at],reference->data[at],(unsigned long)binary->size);break;}}
  CHECK(reference && reference->size==binary->size && !memcmp(reference->data,binary->data,binary->size));
  xe_dxil_shader_binary_delete(reference);
  for(repeat=0;repeat<1000;++repeat)CHECK(xe_ready_owned_get(&cache,key,create,dispose,NULL)==binary);
 }
}
int main(void){sthread_t* threads[3];unsigned i;int before;
 for(i=0;i<3;++i){threads[i]=sthread_create(worker,NULL);CHECK(threads[i]);}
 for(i=0;i<3;++i)sthread_join(threads[i]);
 before=retro_atomic_load_acquire_int(&creates);CHECK(before>=32);
 CHECK(!xe_ready_owned_get(&cache,UINT64_MAX,create,dispose,NULL));CHECK(!xe_ready_owned_get(&cache,UINT64_MAX,create,dispose,NULL));CHECK(retro_atomic_load_acquire_int(&creates)==before+1);
 xe_ready_owned_clear(&cache,dispose);CHECK(retro_atomic_load_acquire_int(&destroys)==before);xe_ready_owned_clear(&cache,dispose);CHECK(retro_atomic_load_acquire_int(&destroys)==before);
 puts("PASS: 32 real native geometry variants across 3 creators, byte-identical direct emission, 96000 borrowed hits, cached failure and exact binary disposal");return 0;
}
