/* Borrowed native PCM state, no frontend/producer concurrency. MSVC C89. */
#include "core_audio_queue.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"queue state line %u\n",(unsigned)__LINE__);return 0;}} while(0)
static float data[3072*6],scratch[3072*6];
static unsigned char saved[3072*6*sizeof(float)];
static int equal_payload(const xe_audio_queue_view *a,const void *bytes) {
  const unsigned char *p=(const unsigned char *)bytes;
  return (!a->bytes[0] || !memcmp(a->data[0],p,a->bytes[0])) &&
      (!a->bytes[1] || !memcmp(a->data[1],p+a->bytes[0],a->bytes[1]));
}
static int unchanged(xe_audio_queue *q,const xe_audio_queue_view *before) {
  xe_audio_queue_view after;
  CHECK(xe_audio_queue_capture(q,&after));
  CHECK(!memcmp(&before->state,&after.state,sizeof(after.state)));
  CHECK(after.data[0]==before->data[0] && after.data[1]==before->data[1]);
  CHECK(after.bytes[0]==before->bytes[0] && after.bytes[1]==before->bytes[1]);
  return 1;
}
static int rejected(xe_audio_queue *q,const xe_audio_queue_view *bad) {
  xe_audio_queue_view before;
  CHECK(xe_audio_queue_capture(q,&before));
  CHECK(!xe_audio_queue_can_restore(q,bad));CHECK(!xe_audio_queue_restore(q,bad));
  CHECK(unchanged(q,&before));return 1;
}
int xe_audio_queue_state_tests(unsigned channels) {
  xe_audio_queue *q;
  xe_audio_queue_view view,external,after,bad,empty;
  size_t i,round,read,bytes,split;
  unsigned wraps=0;
  q=xe_audio_queue_new(channels);CHECK(q);
  for(i=0;i<3072*channels;++i) data[i]=(float)(i%811)-405.0f;
  CHECK(xe_audio_queue_capture(q,&empty));CHECK(!empty.state.frames);
  /* Put the first snapshot across the physical/mirror boundary. Windows'
   * stereo mapping granule is larger than its logical ring capacity. */
  for(round=0;round<(channels==2?2u:1u);++round) {
    CHECK(xe_audio_queue_write(q,data,3072*channels)==3072*channels);
    memset(scratch,0,sizeof(scratch));
    CHECK(xe_audio_queue_mix(q,scratch,3072*channels)==3072*channels);
  }
  for(round=0;round<96;++round) {
    CHECK(xe_audio_queue_write(q,data,3072*channels)==3072*channels);
    read=round*73%1000;memset(scratch,0,read*channels*sizeof(float));
    CHECK(xe_audio_queue_mix(q,scratch,read*channels)==read*channels);
    xe_audio_queue_volume(q,0.25f);xe_audio_queue_pause(q,(int)(round&1));
    CHECK(xe_audio_queue_capture(q,&view));CHECK(view.state.frames==3072-read);
    if(view.bytes[1]) ++wraps;
    bytes=view.bytes[0]+view.bytes[1];
    CHECK(equal_payload(&view,data+read*channels));
    if(view.bytes[0]) memcpy(saved,view.data[0],view.bytes[0]);
    if(view.bytes[1]) memcpy(saved+view.bytes[0],view.data[1],view.bytes[1]);
    CHECK(xe_audio_queue_restore(q,&view));CHECK(unchanged(q,&view));
    external=view;split=round%bytes;
    external.bytes[0]=split;external.data[0]=split?saved:NULL;
    external.bytes[1]=bytes-split;external.data[1]=external.bytes[1]?saved+split:NULL;
    bad=external;bad.state.version=0;CHECK(rejected(q,&bad));
    bad=external;bad.state.channels=channels==2?6:2;CHECK(rejected(q,&bad));
    bad=external;bad.state.frames=3073;CHECK(rejected(q,&bad));
    bad=external;bad.state.paused=2;CHECK(rejected(q,&bad));
    bad=external;bad.bytes[1]++;CHECK(rejected(q,&bad));
    bad=external;bad.data[1]=NULL;CHECK(rejected(q,&bad));
    bad=external;bad.data[1]=(const void *)(UINTPTR_MAX-1);CHECK(rejected(q,&bad));
    bad=view;bad.state.frames--;
    if(bad.bytes[1]>=channels*sizeof(float)) {bad.bytes[1]-=channels*sizeof(float);if(!bad.bytes[1]) bad.data[1]=NULL;}
    else bad.bytes[0]-=channels*sizeof(float);
    CHECK(rejected(q,&bad));
    CHECK(!xe_audio_queue_capture(q,(xe_audio_queue_view *)q));
    CHECK(!xe_audio_queue_capture(q,(xe_audio_queue_view *)view.data[0]));
    CHECK(unchanged(q,&view) && equal_payload(&view,saved));
    memset(scratch,0,sizeof(scratch));CHECK(xe_audio_queue_mix(q,scratch,(3072-read)*channels)==(3072-read)*channels);
    CHECK(!memcmp(scratch,data+read*channels,bytes));
    CHECK(xe_audio_queue_write(q,data,13*channels)==13*channels);
    CHECK(xe_audio_queue_can_restore(q,&external));CHECK(xe_audio_queue_restore(q,&external));
    CHECK(xe_audio_queue_capture(q,&after));CHECK(equal_payload(&after,saved));
    CHECK(!memcmp(&after.state,&view.state,sizeof(view.state)));
    memset(scratch,0,sizeof(scratch));CHECK(xe_audio_queue_mix(q,scratch,3072*channels)==(3072-read)*channels);
    CHECK(!memcmp(scratch,saved,bytes));
    CHECK(xe_audio_queue_restore(q,&empty));
  }
#if defined(XE_AUDIO_QUEUE_PLAIN) && XE_AUDIO_QUEUE_PLAIN
  CHECK(wraps>0);
#else
  (void)wraps;
#endif
  CHECK(xe_audio_queue_write(q,data,3072*channels)==3072*channels);
  CHECK(xe_audio_queue_capture(q,&view) && view.state.frames==3072);
  CHECK(xe_audio_queue_restore(q,&view));CHECK(!xe_audio_queue_can_write_frames(q,1));
  CHECK(xe_audio_queue_restore(q,&empty));CHECK(xe_audio_queue_can_write_frames(q,3072));
  xe_audio_queue_stop(q);CHECK(!xe_audio_queue_restore(q,&empty));
  CHECK(!xe_audio_queue_capture(q,&view));xe_audio_queue_free(q);
  return 1;
}
