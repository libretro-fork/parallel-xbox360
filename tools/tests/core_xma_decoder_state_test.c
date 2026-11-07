/* All-slot decoder capture and malformed metadata. MSVC C89. */
#include "libretro/core_xma_decoder_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <rthreads/rthreads.h>
#include "libretro/core_pause_gate.h"
void *xe_xma_decoder_test_new(unsigned);
void xe_xma_decoder_test_free(void *);
int xe_xma_decoder_test_capture(void *,xe_xma_decoder_view *);
uint32_t xe_xma_decoder_test_allocate(void *);
void xe_xma_decoder_test_release(void *,uint32_t);
const void *xe_xma_decoder_test_registers(void *);
void xe_xma_decoder_test_inconsistent(void *,unsigned);
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"decoder line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
void xe_xma_decoder_test_pending(void *,int);
void *xe_xma_decoder_test_gate(void *);
int xe_xma_decoder_test_rebuild(void *);
int xe_xma_decoder_test_duplicate_setup(void *);
int xe_xma_decoder_test_stream(void *,const void *,unsigned);
void *xe_xma_decoder_test_raw(void *);
const void *xe_xma_decoder_test_drain(void *,unsigned);
static unsigned char packet[2048];
static void put(unsigned *at,unsigned value,unsigned count) {
 unsigned i;for(i=count;i;--i) {
  if((value>>(i-1))&1) packet[*at/8]|=(unsigned char)(1u<<(7-*at%8));
  ++*at;
 }
}
static void acknowledge(void *p) {xe_pause_gate_wait_index((xe_pause_gate *)p,0);}
static void make_packet(unsigned channels) {
 unsigned at=32,bits=channels==1?31:36;
 memset(packet,0,sizeof(packet));packet[0]=4;packet[2]=1;
 put(&at,bits,15);put(&at,1,1);put(&at,0,1);
 if(channels==2) put(&at,0,1);
 put(&at,0,8);put(&at,0,1);put(&at,0,1);put(&at,0,1);
 if(channels==2) {put(&at,0,1);put(&at,2,2);}
 put(&at,0,channels);put(&at,0,2);CHECK(at==32+bits);
}
static xe_xma_decoder_view view;
static xe_xma_decoder_view saved,bad,cold;
static xe_xma_decoder_stage stage,empty_stage;
static uint32_t saved_registers[XE_XMA_REGISTERS];
static unsigned char saved_history[XE_XMA_CONTEXTS][2][2048];
static unsigned char saved_pcm[XE_XMA_CONTEXTS][2048];
int xe_xma_decoder_test_prepare(void *,const xe_xma_decoder_view *,xe_xma_decoder_stage *);
int xe_xma_decoder_test_ready(void *,const xe_xma_decoder_stage *);
int xe_xma_decoder_test_commit(void *,xe_xma_decoder_stage *);
void xe_xma_decoder_test_discard(xe_xma_decoder_stage *);
const void *xe_xma_decoder_test_codec(void *,unsigned);
int xe_xma_decoder_test_rotate(void *,unsigned);
int xe_xma_decoder_test_configure(void *,unsigned,unsigned);
void xe_xma_decoder_test_alloc_limit(size_t);
#ifdef XE_XMA_OPEN_FAIL_TEST
void xe_xma_decoder_test_open_failure(int);
unsigned xe_xma_decoder_test_open_hits(void);
#endif
int xe_xma_decoder_test_load(void *,const void *,unsigned);
const void *xe_xma_decoder_test_guest(void *);
int xe_xma_decoder_test_work(void *);
void xe_xma_decoder_test_cursor(void *,unsigned,unsigned);
void xe_xma_decoder_test_current(void *,unsigned);
#ifdef XE_DECODER_CLOCK_TEST
#include <features/features_cpu.h>
static unsigned clock_reads;
retro_time_t xe_decoder_gate_time(void) {++clock_reads;return cpu_features_get_time_usec();}
#endif
static int capture(void *f) {
 int result;
#ifdef XE_DECODER_CLOCK_TEST
 unsigned before=clock_reads;
#endif
 result=xe_xma_decoder_test_capture(f,&view);
#ifdef XE_DECODER_CLOCK_TEST
 CHECK(clock_reads==before);
#endif
 return result;
}

static void store_snapshot(void *f) {
 unsigned i,c;
 CHECK(capture(f));saved=view;
 memcpy(saved_registers,saved.registers,sizeof(saved_registers));saved.registers=saved_registers;
 for(i=0;i<XE_XMA_CONTEXTS;++i) {
  for(c=0;c<saved.owners[i].state.channels;++c) {
   CHECK(saved.owners[i].codec.history_bytes[c]==2048);
   memcpy(saved_history[i][c],saved.owners[i].codec.history[c],2048);
   saved.owners[i].codec.history[c]=saved_history[i][c];
  }
  if(saved.owners[i].pcm_bytes) {
   CHECK(saved.owners[i].pcm_bytes<=2048);
   memcpy(saved_pcm[i],saved.owners[i].pcm,saved.owners[i].pcm_bytes);
   saved.owners[i].pcm=saved_pcm[i];
  }
 }
}
static void same_snapshot(void *f) {
 unsigned i,c;
 CHECK(capture(f));CHECK(!memcmp(&view.state,&saved.state,sizeof(view.state)));
 CHECK(!memcmp(view.free_slots,saved.free_slots,sizeof(view.free_slots)));
 CHECK(!memcmp(view.registers,saved_registers,sizeof(saved_registers)));
 for(i=0;i<XE_XMA_CONTEXTS;++i) {
  CHECK(!memcmp(&view.owners[i].state,&saved.owners[i].state,sizeof(view.owners[i].state)));
  CHECK(!memcmp(&view.owners[i].codec.state,&saved.owners[i].codec.state,sizeof(view.owners[i].codec.state)));
  for(c=0;c<saved.owners[i].state.channels;++c)
   CHECK(!memcmp(view.owners[i].codec.history[c],saved_history[i][c],2048));
  CHECK(view.owners[i].pcm_bytes==saved.owners[i].pcm_bytes);
  if(view.owners[i].pcm_bytes) CHECK(!memcmp(view.owners[i].pcm,saved_pcm[i],view.owners[i].pcm_bytes));
 }
}
static void restore_tests(void *f,uint32_t first) {
 const void *prefix,*last,*owner;
 struct AVCodecContext *candidate;
 unsigned i;uint32_t physical;
#ifdef XE_DECODER_CLOCK_TEST
 unsigned before=clock_reads;
#endif
 store_snapshot(f);
 prefix=xe_xma_decoder_test_codec(f,0);last=xe_xma_decoder_test_codec(f,319);
 bad=saved;bad.owners[319]=saved.owners[0];
 bad.owners[319].state.id=319;bad.owners[319].state.guest_ptr=first+319*64;
 bad.owners[319].state.allocated=1;bad.owners[319].codec.state.version=0;
 CHECK(xe_xma_decoder_view_valid(&bad,saved.state.context_first,saved.state.context_last));
 CHECK(!xe_xma_decoder_test_prepare(f,&bad,&stage));
 CHECK(!memcmp(&stage,&empty_stage,sizeof(stage)));same_snapshot(f);
 CHECK(xe_xma_decoder_test_codec(f,0)==prefix && xe_xma_decoder_test_codec(f,319)==last);
 bad=saved;bad.owners[0].pcm=&stage;
 CHECK(!xe_xma_decoder_test_prepare(f,&bad,&stage));same_snapshot(f);
 CHECK(!memcmp(&stage,&empty_stage,sizeof(stage)));
 physical=saved_registers[0x600];saved_registers[0x600]^=64;
 CHECK(!xe_xma_decoder_test_prepare(f,&saved,&stage));saved_registers[0x600]=physical;same_snapshot(f);
 CHECK(xe_xma_decoder_test_prepare(f,&saved,&stage));same_snapshot(f);
 for(i=0;i<XE_XMA_CONTEXTS;++i) CHECK(stage.owners[i].candidate && stage.owners[i].owner);
 CHECK(!xe_xma_decoder_test_prepare(f,&saved,&stage));
 CHECK(xe_xma_decoder_test_ready(f,&stage));
 xe_xma_decoder_test_pending(f,1);
 CHECK(!xe_xma_decoder_test_commit(f,&stage));
 xe_xma_decoder_test_pending(f,0);
 stage.free_slots[4]^=1;CHECK(!xe_xma_decoder_test_commit(f,&stage));stage.free_slots[4]^=1;
 owner=stage.owners[319].owner;stage.owners[319].owner=0;
 CHECK(!xe_xma_decoder_test_commit(f,&stage));stage.owners[319].owner=owner;
 candidate=stage.owners[319].candidate;stage.owners[319].candidate=stage.owners[318].candidate;
 CHECK(!xe_xma_decoder_test_commit(f,&stage));stage.owners[319].candidate=candidate;
 stage.owners[319].candidate=(struct AVCodecContext *)stage.owners[318].previous;
 CHECK(!xe_xma_decoder_test_commit(f,&stage));stage.owners[319].candidate=candidate;
 same_snapshot(f);CHECK(xe_xma_decoder_test_codec(f,0)==prefix);
 CHECK(xe_xma_decoder_test_rotate(f,319));
 CHECK(xe_xma_decoder_test_codec(f,319)!=last);
 CHECK(!xe_xma_decoder_test_ready(f,&stage) && !xe_xma_decoder_test_commit(f,&stage));
 CHECK(xe_xma_decoder_test_codec(f,0)==prefix);same_snapshot(f);
 xe_xma_decoder_test_discard(&stage);xe_xma_decoder_test_discard(&stage);
 CHECK(!memcmp(&stage,&empty_stage,sizeof(stage)));
 /* An active final slot borrowing the first owner's PCM is not safe to commit. */
 bad=saved;bad.owners[319]=saved.owners[0];bad.owners[319].state.id=319;
 bad.owners[319].state.guest_ptr=first+319*64;bad.owners[319].state.allocated=1;
 CHECK(capture(f));bad.owners[319].pcm=view.owners[0].pcm;
 CHECK(!xe_xma_decoder_test_prepare(f,&bad,&stage));same_snapshot(f);
 CHECK(!memcmp(&stage,&empty_stage,sizeof(stage)));
 for(i=0;i<3;++i) {
  CHECK(xe_xma_decoder_test_prepare(f,&saved,&stage));
  ((uint32_t *)xe_xma_decoder_test_registers(f))[0x607]=77;
  xe_xma_decoder_test_release(f,first+64);
  CHECK(xe_xma_decoder_test_commit(f,&stage));
  CHECK(!memcmp(&stage,&empty_stage,sizeof(stage)));same_snapshot(f);
  CHECK(!xe_xma_decoder_test_commit(f,&stage));
 }
 /* Borrow the exact live register/PCM storage; no register copy is needed. */
 CHECK(capture(f));CHECK(xe_xma_decoder_test_prepare(f,&view,&stage));
 CHECK(xe_xma_decoder_test_commit(f,&stage));same_snapshot(f);
 CHECK(!memcmp(&stage,&empty_stage,sizeof(stage)));
#ifdef XE_DECODER_CLOCK_TEST
 CHECK(clock_reads==before);
#endif
}

static void begin_codec_failure(unsigned kind) {
 xe_xma_decoder_test_alloc_limit(kind?INT_MAX:1);
#ifdef XE_XMA_OPEN_FAIL_TEST
 xe_xma_decoder_test_open_failure(kind!=0);
#endif
}
static void end_codec_failure(unsigned kind) {
 xe_xma_decoder_test_alloc_limit(INT_MAX);
#ifdef XE_XMA_OPEN_FAIL_TEST
 if(kind) CHECK(xe_xma_decoder_test_open_hits()==1);
 xe_xma_decoder_test_open_failure(0);
#else
 (void)kind;
#endif
}
static void configuration_tests(void *f,uint32_t first) {
 const void *token;
 unsigned i,faults=1;
 unsigned char guest[64];
#ifdef XE_XMA_OPEN_FAIL_TEST
 faults=2;
#endif
 cold=view;
 CHECK(xe_xma_decoder_test_allocate(f)==first);
 store_snapshot(f);token=xe_xma_decoder_test_codec(f,0);
 for(i=0;i<faults;++i) {
  begin_codec_failure(i);
  CHECK(xe_xma_decoder_test_configure(f,3,1)==-1);
  end_codec_failure(i);
  CHECK(xe_xma_decoder_test_codec(f,0)==token);same_snapshot(f);
 }
 CHECK(xe_xma_decoder_test_configure(f,3,1)==1);
 store_snapshot(f);token=xe_xma_decoder_test_codec(f,0);
 for(i=0;i<faults;++i) {
  begin_codec_failure(i);
  CHECK(xe_xma_decoder_test_configure(f,3,1)==0);
  CHECK(xe_xma_decoder_test_configure(f,2,2)==-1);
  end_codec_failure(i);
  CHECK(xe_xma_decoder_test_codec(f,0)==token);same_snapshot(f);
 }
 make_packet(2);CHECK(xe_xma_decoder_test_load(f,packet,2));
 xe_xma_decoder_test_cursor(f,0x3ffffff,0);store_snapshot(f);
 CHECK(xe_xma_decoder_test_work(f));
 CHECK(xe_xma_decoder_test_codec(f,0)==token);same_snapshot(f);
 xe_xma_decoder_test_cursor(f,32,2);
 xe_xma_decoder_test_current(f,1);
 memcpy(guest,xe_xma_decoder_test_guest(f),sizeof(guest));store_snapshot(f);
 for(i=0;i<faults;++i) {
  begin_codec_failure(i);CHECK(xe_xma_decoder_test_work(f));
  end_codec_failure(i);
  CHECK(xe_xma_decoder_test_codec(f,0)==token);same_snapshot(f);
  CHECK(!memcmp(guest,xe_xma_decoder_test_guest(f),sizeof(guest)));
 }
 xe_xma_decoder_test_cursor(f,32,0);
 xe_xma_decoder_test_current(f,0);
 CHECK(xe_xma_decoder_test_work(f));CHECK(capture(f));
 CHECK(xe_xma_decoder_test_codec(f,0)!=token);
 CHECK(view.owners[0].state.channels==2 && view.owners[0].state.pending_blocks==5);
 CHECK(xe_xma_decoder_test_prepare(f,&cold,&stage));
 CHECK(xe_xma_decoder_test_commit(f,&stage));CHECK(capture(f));
 for(i=0;i<XE_XMA_CONTEXTS;++i) CHECK(!view.owners[i].state.rate && !view.owners[i].state.allocated);
}

int main(void) {
 void *f;uint32_t first,last,i,mode;unsigned c,n;unsigned char *raw,expected[256];const void *drained;sthread_t *worker;xe_pause_gate *gate;
 f=xe_xma_decoder_test_new(XE_XMA_MODE_RAW);CHECK(f);
 CHECK(capture(f));first=view.state.context_first;last=view.state.context_last;
 CHECK(xe_xma_decoder_test_duplicate_setup(f));
 CHECK(capture(f));
 CHECK(view.registers==xe_xma_decoder_test_registers(f) && view.register_bytes==65536);
 CHECK(xe_xma_decoder_test_prepare(f,&view,&stage));
 CHECK(xe_xma_decoder_test_commit(f,&stage));CHECK(capture(f));
 configuration_tests(f,first);
 gate=(xe_pause_gate *)xe_xma_decoder_test_gate(f);
 for(i=0;i<2;++i) {
  xe_xma_decoder_test_pending(f,1);
  CHECK(!capture(f) && !view.state.version);
  worker=sthread_create(acknowledge,gate);CHECK(worker);
  CHECK(xe_pause_gate_wait_workers(gate,1,1000000));
  CHECK(capture(f));
  xe_pause_gate_work_begin(gate);CHECK(!capture(f) && !view.state.version);
  xe_pause_gate_work_end(gate);CHECK(capture(f));
  xe_xma_decoder_test_pending(f,0);sthread_join(worker);
 }
 for(i=0;i<320;++i) {
  CHECK(!view.owners[i].state.rate && !view.owners[i].pcm_bytes && !view.owners[i].state.allocated);
  CHECK(xe_xma_decoder_test_allocate(f)==first+i*64);
 }
 CHECK(!xe_xma_decoder_test_allocate(f));CHECK(capture(f));
 for(i=0;i<5;++i) CHECK(!view.free_slots[i]);
 for(c=1;c<=2;++c) {
  raw=(unsigned char *)xe_xma_decoder_test_raw(f);memset(raw,0xa5,2048);
  make_packet(c);CHECK(xe_xma_decoder_test_stream(f,packet,c));CHECK(capture(f));
  CHECK(view.owners[0].state.pending_blocks==c*4-3);
  for(n=0;n<c*1024;++n) CHECK(raw[n]==0);
  raw=(unsigned char *)xe_xma_decoder_test_raw(f);
  for(n=0;n<2048;++n) raw[n]=(unsigned char)(n/256+1);
  restore_tests(f,first);
  drained=xe_xma_decoder_test_drain(f,c==1);CHECK(drained);
  memset(expected,4,sizeof(expected));CHECK(!memcmp(drained,expected,sizeof(expected)));
  CHECK(capture(f));CHECK(view.owners[0].state.pending_blocks==c*4-4);
 }
 make_packet(1);CHECK(xe_xma_decoder_test_stream(f,packet,1));CHECK(capture(f));
 CHECK(view.owners[0].state.rate==48000 && view.owners[0].state.channels==1);
 CHECK(view.owners[0].state.pending_blocks==1 && view.owners[0].pcm_bytes==256);
 CHECK(view.owners[0].codec.history_bytes[0]==2048);
 for(i=0;i<320;i+=3) xe_xma_decoder_test_release(f,first+i*64);
 CHECK(capture(f));CHECK(xe_xma_decoder_view_valid(&view,first,last));
 for(i=0;i<320;++i) CHECK(view.owners[i].state.allocated==(uint32_t)(i%3!=0));
 CHECK(!view.owners[0].state.allocated && view.owners[0].state.rate==48000 && view.owners[0].pcm_bytes==256);
 restore_tests(f,first);
 view.free_slots[4]^=1;CHECK(!xe_xma_decoder_view_valid(&view,first,last));view.free_slots[4]^=1;
 view.owners[319].state.guest_ptr++;CHECK(!xe_xma_decoder_view_valid(&view,first,last));
 CHECK(capture(f));
 view.register_bytes--;CHECK(!xe_xma_decoder_view_valid(&view,first,last));
 CHECK(capture(f));view.state.count--;CHECK(!xe_xma_decoder_view_valid(&view,first,last));
 CHECK(capture(f));view.state.mode=XE_XMA_MODE_OLD;CHECK(!xe_xma_decoder_view_valid(&view,first,last));
 CHECK(capture(f));view.owners[319].codec.state.version=1;CHECK(!xe_xma_decoder_view_valid(&view,first,last));
 CHECK(capture(f));view.registers=(const uint32_t *)(UINTPTR_MAX-7);CHECK(!xe_xma_decoder_view_valid(&view,first,last));
 CHECK(capture(f));
 xe_xma_decoder_test_inconsistent(f,319);CHECK(!capture(f) && !view.state.version);
 xe_xma_decoder_test_inconsistent(f,319);CHECK(capture(f));
 CHECK(xe_xma_decoder_test_rebuild(f));CHECK(capture(f));
 for(i=0;i<320;++i) CHECK(!view.owners[i].state.allocated);
 for(i=0;i<5;++i) CHECK(view.free_slots[i]==UINT64_MAX);
 xe_xma_decoder_test_free(f);
 for(mode=0;mode<XE_XMA_MODE_RAW;++mode) {
  f=xe_xma_decoder_test_new(mode);CHECK(f);
  view.state.version=1;CHECK(!capture(f) && !view.state.version);
  xe_xma_decoder_test_free(f);
 }
#ifdef XE_DECODER_CLOCK_TEST
 CHECK(clock_reads>0);
#endif
#ifdef XE_XMA_OPEN_FAIL_TEST
 puts("PASS: actual codec-open allocation failures retain cold/warm owners and guest cursors; later retry succeeds");
#endif
 puts("PASS: allocation failure/retry, 320-owner transactional restore/reuse, late failure without prefix mutation, aliases, clock-free quiescence, PCM byte oracles and unsupported styles");
 return 0;
}
