/* Independent variants must translate without the native emission mutex. C89. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "shader_call_fixture.h"
#include "src/xenia/gpu/dxil/guest_texture_stage.h"
void* xe_binding_translate_new(const uint32_t*,size_t,unsigned);
void xe_binding_translate_free(void*);
void xe_binding_translate_lock(void*,unsigned);
int xe_binding_translate_step(void*,unsigned);
int xe_binding_translate_late(const uint32_t*,size_t,unsigned,const XeDxilTextureLayout*);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"binding translate line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static retro_eventcount_t gate;
static retro_atomic_int_t start,done;
struct context{void* owner;unsigned index;int result;};
static void wait_value(retro_atomic_int_t* value,int expected){int key;
 while(retro_atomic_load_acquire_int(value)<expected){key=retro_eventcount_prepare_wait(&gate);
 if(retro_atomic_load_acquire_int(value)>=expected)retro_eventcount_cancel_wait(&gate);
 else CHECK(retro_eventcount_commit_wait_timeout(&gate,key,5000000) || retro_atomic_load_acquire_int(value)>=expected);}}
static void worker(void* opaque){struct context* c=(struct context*)opaque;wait_value(&start,1);
 c->result=xe_binding_translate_step(c->owner,c->index);retro_atomic_fetch_add_int(&done,1);retro_eventcount_notify(&gate);}
static size_t texture_scenario(uint32_t code[51],unsigned pixel){unsigned i;
 memset(code,0,51*sizeof(*code));cf(code,0,1u|(3u<<12)|(5u<<16),0x2000);
 for(i=0;i<2;++i){code[3+i*3]=1u|((i+1)<<20)|(36u<<26);
 code[4+i*3]=0x688u|(3u<<12)|(3u<<14)|(3u<<16)|(7u<<18)|(1u<<29);
 code[5+i*3]=(i+1)<<14;}
 code[9]=(pixel?0u:62u)|(1u<<15)|(15u<<16)|(50u<<26);code[11]=0xc2000000;return 12;}
int main(void){unsigned pixel,mode,i,cases=0;uint32_t code[51];size_t count;void* owner;sthread_t* threads[8];struct context work[8];XeDxilTextureLayout late;
 CHECK(retro_eventcount_init(&gate));
 for(pixel=0;pixel<2;++pixel)for(mode=0;mode<6;++mode){count=mode==5?texture_scenario(code,pixel):nested_scenario(code,pixel,mode);owner=xe_binding_translate_new(code,count,pixel);CHECK(owner);
 retro_atomic_store_release_int(&start,0);retro_atomic_store_release_int(&done,0);xe_binding_translate_lock(owner,1);
 for(i=0;i<8;++i){work[i].owner=owner;work[i].index=i;work[i].result=0;threads[i]=sthread_create(worker,&work[i]);CHECK(threads[i]);}
 retro_atomic_store_release_int(&start,1);retro_eventcount_notify(&gate);wait_value(&done,8);
 xe_binding_translate_lock(owner,0);
 for(i=0;i<8;++i){sthread_join(threads[i]);if(work[i].result!=1)fprintf(stderr,"pixel %u mode %u variant %u result %d\n",pixel,mode,i,work[i].result);CHECK(work[i].result==1);++cases;}xe_binding_translate_free(owner);}
 for(pixel=0;pixel<2;++pixel){count=nested_scenario(code,pixel,0);memset(&late,0,sizeof(late));
 CHECK(xe_binding_translate_late(code,count,pixel,&late)==1);
 late.used_texture_mask=1;CHECK(xe_binding_translate_late(code,count,pixel,&late)==0);
 late.used_texture_mask=0;late.sampler_count=1;CHECK(xe_binding_translate_late(code,count,pixel,&late)==0);
 late.sampler_count=0;late.texture_count=1;CHECK(xe_binding_translate_late(code,count,pixel,&late)==0);}
 retro_eventcount_free(&gate);printf("PASS: %u real VS/PS variants translate under held native-emission mutex, exact cold/preloaded references, late-publisher mismatch rejection and joined destruction\n",cases);return 0;}
