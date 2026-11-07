/* All-slot decoder capture and malformed metadata. MSVC C89. */
#include "libretro/core_xma_decoder_state.h"
#include <stdio.h>
#include <stdlib.h>
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

int main(void) {
 void *f;uint32_t first,last,i,mode;unsigned c,n;unsigned char *raw,expected[256];const void *drained;sthread_t *worker;xe_pause_gate *gate;
 f=xe_xma_decoder_test_new(XE_XMA_MODE_RAW);CHECK(f);
 CHECK(capture(f));first=view.state.context_first;last=view.state.context_last;
 CHECK(xe_xma_decoder_test_duplicate_setup(f));
 CHECK(capture(f));
 CHECK(view.registers==xe_xma_decoder_test_registers(f) && view.register_bytes==65536);
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
 puts("PASS: 320 actual owners, active/unallocated PCM, acknowledged capture without clocks, borrowed registers, bitmap agreement, malformed final slot and unsupported styles");
 return 0;
}
