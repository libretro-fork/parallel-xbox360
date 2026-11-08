/* Exact native PCM/resampler continuation; MSVC C89. */
#include "libretro/core_audio_driver_state.h"
#include "libretro/core_apu_clients.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *xe_driver_state_new(unsigned,unsigned,unsigned,unsigned);
void xe_driver_state_free(void *);
void xe_driver_state_submit(void *,float *);
int xe_driver_state_submit_media(void *,const float *,unsigned);
int xe_driver_state_capture(void *,xe_audio_driver_view *);
int xe_driver_state_ready(void *,const xe_audio_driver_view *);
int xe_driver_state_restore(void *,const xe_audio_driver_view *);
void xe_driver_state_stop(void *);
void *xe_driver_state_new_credit(const xe_apu_credit_sink *);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"driver state line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
#ifdef XE_AUDIO_CHECKPOINT_ALLOC_TEST
static size_t allocation_calls,free_calls;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) {++allocation_calls;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s) {++allocation_calls;return __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n) {++allocation_calls;return __real_realloc(p,n);}
void __wrap_free(void *p) {++free_calls;__real_free(p);}
#endif
static float input[1536];
static unsigned char saved_pcm[3072*6*4],expected_pcm[3072*6*4];
static xe_audio_driver_view saved,expected,actual,bad;
static void copy_pcm(const xe_audio_driver_view *view,void *out) {
  unsigned char *p=(unsigned char *)out;
  if(view->queue.bytes[0]) memcpy(p,view->queue.data[0],view->queue.bytes[0]);
  if(view->queue.bytes[1]) memcpy(p+view->queue.bytes[0],view->queue.data[1],view->queue.bytes[1]);
}
static void same(const xe_audio_driver_view *a,const xe_audio_driver_view *b,const void *pcm) {
  const unsigned char *p=(const unsigned char *)pcm;
  CHECK(a->version==b->version && a->frequency==b->frequency && a->channels==b->channels);
  CHECK(a->output_channels==b->output_channels && a->channel_frames==b->channel_frames && a->planar_be==b->planar_be);
  CHECK(!memcmp(&a->resample_position,&b->resample_position,sizeof(a->resample_position)));
  CHECK(!memcmp(a->last,b->last,sizeof(a->last)));
  CHECK(!memcmp(&a->queue.state,&b->queue.state,sizeof(a->queue.state)));
  CHECK(a->queue.bytes[0]+a->queue.bytes[1]==b->queue.bytes[0]+b->queue.bytes[1]);
  if(a->queue.bytes[0]) CHECK(!memcmp(a->queue.data[0],p,a->queue.bytes[0]));
  if(a->queue.bytes[1]) CHECK(!memcmp(a->queue.data[1],p+a->queue.bytes[0],a->queue.bytes[1]));
}
static void rejected(void *driver,const xe_audio_driver_view *view) {
  CHECK(!xe_driver_state_ready(driver,view));CHECK(!xe_driver_state_restore(driver,view));
  CHECK(xe_driver_state_capture(driver,&actual));same(&actual,&expected,expected_pcm);
}
static void samples(unsigned planar,unsigned pass) {
  size_t i;uint32_t bits;float f;unsigned char *p;
  for(i=0;i<1536;++i) {
    f=((float)((i*17+pass*113)%503)-251.0f)/512.0f;
    if(!planar) input[i]=f;
    else {memcpy(&bits,&f,4);p=(unsigned char *)&input[i];p[0]=(unsigned char)(bits>>24);p[1]=(unsigned char)(bits>>16);p[2]=(unsigned char)(bits>>8);p[3]=(unsigned char)bits;}
  }
}
static void real_credit_driver(void) {
  xe_apu_clients *clients=xe_apu_clients_new(8);
  xe_apu_credit_sink sink;uint64_t token,lease;unsigned index,count;void *driver;
  CHECK(clients);CHECK(xe_apu_clients_reserve(clients,&index,&token));
  CHECK(xe_apu_clients_publish(clients,index,token,1));
  sink.owner=clients;sink.index=index;sink.token=token;
  driver=xe_driver_state_new_credit(&sink);CHECK(driver);
  CHECK(xe_apu_clients_acquire(clients,index,&lease));
  CHECK(xe_apu_clients_credits(clients,index,token,&count) && !count);
  xe_driver_state_submit(driver,input);
  CHECK(xe_apu_clients_credits(clients,index,token,&count) && count==1);
  CHECK(xe_apu_clients_release(clients,index,lease)==0);
  xe_driver_state_free(driver);
  CHECK(xe_apu_clients_retire(clients,index,&token)==XE_APU_RETIRE_OWN);
  CHECK(xe_apu_clients_finish(clients,index,token));xe_apu_clients_free(clients);
}
static void partial_media_test(void) {
 static const float values[12] = {1.000003f,-1.000003f,0.000000013f,-0.000000013f,
   0.1234567f,-0.7654321f,2.0f,-2.0f,0.5f,-0.5f,0.0f,-0.0f};
 void *driver=xe_driver_state_new(48000,2,2,0);
 CHECK(driver && xe_driver_state_submit_media(driver,values,6));
 CHECK(xe_driver_state_capture(driver,&actual));
 CHECK(actual.queue.bytes[0]+actual.queue.bytes[1]==sizeof(values));
 copy_pcm(&actual,expected_pcm);CHECK(!memcmp(expected_pcm,values,sizeof(values)));
 CHECK(!xe_driver_state_submit_media(driver,values,0));
 CHECK(!xe_driver_state_submit_media(driver,values,769));
 CHECK(xe_driver_state_capture(driver,&actual));
 CHECK(actual.queue.bytes[0]+actual.queue.bytes[1]==sizeof(values));
 xe_driver_state_free(driver);
 puts("PASS: partial float media preserves tiny values, overshoot and exact tail length");
}
int main(void) {
  static const unsigned rates[]={48000,44100,32000};
  unsigned r,channels,output,planar,count=0;void *driver;uint64_t bits;
#ifdef XE_AUDIO_CHECKPOINT_ALLOC_TEST
  size_t before_alloc,before_free;
#endif
  real_credit_driver();
  for(r=0;r<3;++r) for(channels=2;channels<=6;channels+=4)
    for(output=2;output<=6;output+=4) for(planar=0;planar<=(unsigned)(channels==6);++planar) {
    driver=xe_driver_state_new(rates[r],channels,output,planar);CHECK(driver);
#ifdef XE_AUDIO_CHECKPOINT_ALLOC_TEST
    before_alloc=allocation_calls;before_free=free_calls;
#endif
    samples(planar,0);xe_driver_state_submit(driver,input);
    CHECK(xe_driver_state_capture(driver,&saved));copy_pcm(&saved,saved_pcm);
    saved.queue.bytes[0]+=saved.queue.bytes[1];saved.queue.bytes[1]=0;
    saved.queue.data[0]=saved_pcm;saved.queue.data[1]=NULL;
    samples(planar,1);xe_driver_state_submit(driver,input);
    CHECK(xe_driver_state_capture(driver,&expected));copy_pcm(&expected,expected_pcm);
    bad=saved;bad.version=0;rejected(driver,&bad);
    bad=saved;bad.frequency++;rejected(driver,&bad);
    bad=saved;bad.channel_frames++;rejected(driver,&bad);
    bad=saved;bad.planar_be^=1;rejected(driver,&bad);
    bad=saved;bad.resample_position=-1.0;rejected(driver,&bad);
    bad=saved;bad.resample_position=(double)rates[r]/48000.0;rejected(driver,&bad);
    bad=saved;bits=UINT64_C(0x7ff8000000000000);memcpy(&bad.resample_position,&bits,8);rejected(driver,&bad);
    bad=saved;bad.queue.bytes[0]++;rejected(driver,&bad);
    CHECK(!xe_driver_state_capture(driver,(xe_audio_driver_view *)driver));
    rejected(driver,(const xe_audio_driver_view *)driver);
    CHECK(xe_driver_state_ready(driver,&saved));CHECK(xe_driver_state_restore(driver,&saved));
    xe_driver_state_submit(driver,input);
    CHECK(xe_driver_state_capture(driver,&actual));same(&actual,&expected,expected_pcm);
    CHECK(xe_driver_state_restore(driver,&actual));
    CHECK(xe_driver_state_capture(driver,&actual));same(&actual,&expected,expected_pcm);
    xe_driver_state_stop(driver);CHECK(!xe_driver_state_restore(driver,&saved));
#ifdef XE_AUDIO_CHECKPOINT_ALLOC_TEST
    CHECK(allocation_calls==before_alloc && free_calls==before_free);
#endif
    xe_driver_state_free(driver);++count;
  }
  partial_media_test();
  printf("PASS: %u native audio driver continuations, exact PCM/carry, format/phase/alias rejection and stopped-state safety\n",count);
#ifdef XE_AUDIO_CHECKPOINT_ALLOC_TEST
 puts("PASS: zero linked malloc/calloc/realloc/free calls after initialization through checkpoint, continuation and stop");
#endif
  return 0;
}
