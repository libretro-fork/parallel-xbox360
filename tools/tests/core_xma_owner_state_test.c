/* Real-stream owner checkpoint/replay. MSVC C89. No guest CPU or device. */
#include "libretro/core_xma_owner_state.h"
#include <stdio.h>
#include <stdlib.h>
void *xe_xma_owner_test_new(const void *,unsigned,unsigned,unsigned);
void xe_xma_owner_test_free(void *);
void *xe_xma_owner_test_context(void *);
const void *xe_xma_owner_test_output(void *);
int xe_xma_owner_test_work(void *);
void xe_xma_owner_test_ring(void *,unsigned *,unsigned *,unsigned *);
void xe_xma_owner_test_read(void *,unsigned);
int xe_xma_owner_test_capture(void *,xe_xma_owner_view *);
int xe_xma_owner_test_prepare(void *,const xe_xma_owner_view *,xe_xma_owner_stage *);
int xe_xma_owner_test_commit(void *,xe_xma_owner_stage *);
int xe_xma_owner_test_wrong_owner(xe_xma_owner_stage *);
void xe_xma_owner_test_discard(xe_xma_owner_stage *);
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"owner line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static unsigned char input[131072],pcm[2048];
static float history[2][256];
static unsigned char guest[64],ring[8192],initial_guest[64],initial_ring[8192];
static unsigned char reference[65536],replayed[65536];
static void store(xe_xma_owner_view *view) {
 unsigned c;
 for(c=0;c<view->state.channels;++c) {
  CHECK(view->codec.history_samples[c]==256);
  memcpy(history[c],view->codec.history[c],sizeof(history[c]));view->codec.history[c]=history[c];
 }
 if(view->pcm_bytes) {CHECK(view->pcm_bytes<=sizeof(pcm));memcpy(pcm,view->pcm,view->pcm_bytes);view->pcm=pcm;}
}
static unsigned future(void *f,unsigned capacity,unsigned char *out) {
 unsigned n,read,write,valid,available,take,first,total=0;
 const unsigned char *data=(const unsigned char *)xe_xma_owner_test_output(f);
 for(n=0;n<96;++n) {
  xe_xma_owner_test_ring(f,&read,&write,&valid);
  available=(write+capacity-read)%capacity;
  if(!available && !valid) available=capacity;
  take=available<512?available:512;first=take<capacity-read?take:capacity-read;
  CHECK(total+take<=sizeof(reference));
  memcpy(out+total,data+read,first);memcpy(out+total+first,data,take-first);total+=take;
  xe_xma_owner_test_read(f,(read+take)%capacity);
  CHECK(xe_xma_owner_test_work(f));
 }
 CHECK(total>32000);return total;
}
static void rejected(void *f,const xe_xma_owner_view *bad) {
 xe_xma_owner_view before,after;
 xe_xma_owner_stage stage={0},empty={0};
 float saved[2][256];unsigned char pending[2048];unsigned c;
 CHECK(xe_xma_owner_test_capture(f,&before));
 for(c=0;c<before.state.channels;++c) memcpy(saved[c],before.codec.history[c],sizeof(saved[c]));
 if(before.pcm_bytes) memcpy(pending,before.pcm,before.pcm_bytes);
 CHECK(!xe_xma_owner_test_prepare(f,bad,&stage));CHECK(!memcmp(&stage,&empty,sizeof(stage)));
 CHECK(xe_xma_owner_test_capture(f,&after));CHECK(!memcmp(&before.state,&after.state,sizeof(before.state)));
 CHECK(!memcmp(&before.codec.state,&after.codec.state,sizeof(before.codec.state)));
 for(c=0;c<before.state.channels;++c) CHECK(!memcmp(saved[c],after.codec.history[c],sizeof(saved[c])));
 CHECK(before.pcm_bytes==after.pcm_bytes);
 if(before.pcm_bytes) CHECK(!memcmp(pending,after.pcm,after.pcm_bytes));
}
int main(void) {
 const char *path=getenv("XMA_INPUT_RAW");FILE *file;void *f;
 unsigned bytes,blocks,quantum,count=0,partial=0,a,b;
 xe_xma_owner_view initial,saved,bad,after;
 xe_xma_owner_stage stage={0},stale={0},empty={0};
 CHECK(path);file=fopen(path,"rb");CHECK(file);
 bytes=(unsigned)fread(input,1,sizeof(input),file);CHECK(!ferror(file));CHECK(!fclose(file));
 CHECK(bytes && !(bytes%2048));
 for(blocks=9;blocks<=17;blocks+=4) for(quantum=1;quantum<=4;quantum*=2) {
  f=xe_xma_owner_test_new(input,bytes,blocks,quantum);CHECK(f);
  CHECK(xe_xma_owner_test_capture(f,&initial));CHECK(!initial.state.rate && !initial.pcm_bytes);
  memcpy(initial_guest,xe_xma_owner_test_context(f),64);memcpy(initial_ring,xe_xma_owner_test_output(f),blocks*256);
  CHECK(xe_xma_owner_test_work(f));CHECK(xe_xma_owner_test_capture(f,&saved));
  CHECK(saved.state.rate==48000 && saved.state.channels==2);
  if(saved.state.pending_blocks) ++partial;
  store(&saved);memcpy(guest,xe_xma_owner_test_context(f),64);memcpy(ring,xe_xma_owner_test_output(f),blocks*256);
  bad=saved;bad.state.version=0;rejected(f,&bad);
  bad=saved;bad.state.id++;rejected(f,&bad);
  bad=saved;bad.state.guest_ptr++;rejected(f,&bad);
  bad=saved;bad.state.allocated=2;rejected(f,&bad);
  bad=saved;bad.state.enabled=2;rejected(f,&bad);
  bad=saved;bad.state.loop_start_pending=2;rejected(f,&bad);
  bad=saved;bad.state.loop_limit=17;rejected(f,&bad);
  bad=saved;bad.state.pending_blocks=9;rejected(f,&bad);
  bad=saved;bad.state.rate=32000;rejected(f,&bad);
  bad=saved;bad.codec.state.version=0;rejected(f,&bad);
  bad=saved;bad.codec.history_samples[1]--;rejected(f,&bad);
  bad=saved;bad.pcm_bytes++;rejected(f,&bad);
  CHECK(xe_xma_owner_test_prepare(f,&saved,&stage));
  CHECK(!xe_xma_owner_test_prepare(f,&saved,&stage));
  CHECK(!xe_xma_owner_test_wrong_owner(&stage));
  xe_xma_owner_test_discard(&stage);xe_xma_owner_test_discard(&stage);
  CHECK(!memcmp(&stage,&empty,sizeof(stage)));
  a=future(f,blocks*256,reference);
  CHECK(xe_xma_owner_test_prepare(f,&saved,&stage));
  memcpy(xe_xma_owner_test_context(f),guest,64);memcpy((void *)xe_xma_owner_test_output(f),ring,blocks*256);
  CHECK(xe_xma_owner_test_commit(f,&stage));CHECK(!xe_xma_owner_test_commit(f,&stage));
  CHECK(xe_xma_owner_test_capture(f,&after));CHECK(!memcmp(&saved.state,&after.state,sizeof(saved.state)));
  b=future(f,blocks*256,replayed);CHECK(a==b && !memcmp(reference,replayed,a));
  bad=saved;bad.state.enabled=1;bad.state.allocated=0;
  bad.state.loop_limit=16;bad.state.loop_start_pending=1;
  CHECK(xe_xma_owner_test_prepare(f,&bad,&stage));CHECK(xe_xma_owner_test_commit(f,&stage));
  CHECK(xe_xma_owner_test_capture(f,&after));CHECK(!memcmp(&bad.state,&after.state,sizeof(bad.state)));
  CHECK(xe_xma_owner_test_prepare(f,&saved,&stage));
  CHECK(xe_xma_owner_test_prepare(f,&saved,&stale));
  CHECK(xe_xma_owner_test_commit(f,&stage));CHECK(!xe_xma_owner_test_commit(f,&stale));
  xe_xma_owner_test_discard(&stale);
  CHECK(xe_xma_owner_test_prepare(f,&initial,&stage));
  memcpy(xe_xma_owner_test_context(f),initial_guest,64);memcpy((void *)xe_xma_owner_test_output(f),initial_ring,blocks*256);
  CHECK(xe_xma_owner_test_commit(f,&stage));CHECK(xe_xma_owner_test_capture(f,&after));
  CHECK(!after.state.rate && !after.pcm_bytes);
  CHECK(xe_xma_owner_test_work(f));b=future(f,blocks*256,replayed);
  CHECK(a==b && !memcmp(reference,replayed,a));
  xe_xma_owner_test_free(f);++count;
 }
 CHECK(partial);
 printf("PASS: %u real XMA owner replays (%u partial PCM), cold/open restore and rejected-state nonmutation\n",count,partial);
 return 0;
}
