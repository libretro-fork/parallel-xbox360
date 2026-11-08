/* Native object/bitfield/decoder lifetime bridge; fixture algorithms are C89. */
#include "xenia/memory.h"
#include "xenia/apu/xma_context_new.h"
#include "xenia/apu/xma_helpers.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <new>
struct OwnerFixture {
 xe::Memory memory;
 xe::apu::XmaContextNew decoder;
 uint32_t context_address=0,input_address=0,output_address=0;
 uint32_t physical_output=0;
};
extern "C" void xe_xma_owner_test_free(void *p) {delete static_cast<OwnerFixture*>(p);}
extern "C" void *xe_xma_owner_test_new(const void *input,unsigned bytes,unsigned blocks,unsigned quantum) {
 static bool logging=false;
 if(!logging) {xe::InitializeLogging("xma_owner_state_test");xe::Profiler::Initialize();logging=true;}
 OwnerFixture *f=new(std::nothrow) OwnerFixture;
 if(!f) return nullptr;
 if(!f->memory.Initialize()) goto fail;
 f->context_address=f->memory.SystemHeapAlloc(4096);
 f->input_address=f->memory.SystemHeapAlloc(131072,4096,xe::kSystemHeapPhysical);
 f->output_address=f->memory.SystemHeapAlloc(8192,4096,xe::kSystemHeapPhysical);
 if(!f->context_address || !f->input_address || !f->output_address || bytes>131072) goto fail;
 {
  uint8_t *context=f->memory.TranslateVirtual(f->context_address);
  uint32_t physical_input=f->memory.GetPhysicalAddress(f->input_address);
  f->physical_output=f->memory.GetPhysicalAddress(f->output_address);
  memset(context,0,64);memset(f->memory.TranslatePhysical(f->physical_output),0,8192);
  memcpy(f->memory.TranslatePhysical(physical_input),input,bytes);
  if(f->decoder.Setup(0,&f->memory,f->context_address)) goto fail;
  f->decoder.set_is_allocated(true);
  xe::apu::XMA_CONTEXT_DATA state(context);
  state.input_buffer_0_ptr=physical_input;state.input_buffer_0_packet_count=bytes/2048;
  state.input_buffer_0_valid=1;
  state.input_buffer_read_offset=xe::apu::xma::GetPacketFrameOffset(static_cast<const uint8_t*>(input));
  state.output_buffer_ptr=f->physical_output;state.output_buffer_block_count=blocks;
  state.subframe_decode_count=quantum;state.is_stereo=1;state.sample_rate=3;
  state.output_buffer_valid=1;state.Store(context);
 }
 return f;
fail: delete f;return nullptr;
}
extern "C" void *xe_xma_owner_test_context(void *p) {
 OwnerFixture *f=static_cast<OwnerFixture*>(p);return f->memory.TranslateVirtual(f->context_address);
}
extern "C" const void *xe_xma_owner_test_output(void *p) {
 OwnerFixture *f=static_cast<OwnerFixture*>(p);return f->memory.TranslatePhysical(f->physical_output);
}
extern "C" int xe_xma_owner_test_work(void *p) {
 OwnerFixture *f=static_cast<OwnerFixture*>(p);
 xe::apu::XMA_CONTEXT_DATA data(xe_xma_owner_test_context(p));
 data.output_buffer_valid=1;data.Store(xe_xma_owner_test_context(p));
 f->decoder.Enable();return f->decoder.Work();
}
extern "C" void xe_xma_owner_test_ring(void *p,unsigned *read,unsigned *write,unsigned *valid) {
 xe::apu::XMA_CONTEXT_DATA data(xe_xma_owner_test_context(p));
 *read=data.output_buffer_read_offset*256;*write=data.output_buffer_write_offset*256;
 *valid=data.output_buffer_valid;
}
extern "C" void xe_xma_owner_test_read(void *p,unsigned read) {
 xe::apu::XMA_CONTEXT_DATA data(xe_xma_owner_test_context(p));
 data.output_buffer_read_offset=read/256;data.Store(xe_xma_owner_test_context(p));
}
extern "C" int xe_xma_owner_test_capture(void *p,xe_xma_owner_view *v) {
 return static_cast<OwnerFixture*>(p)->decoder.CaptureState(v);
}
extern "C" int xe_xma_owner_test_prepare(void *p,const xe_xma_owner_view *v,xe_xma_owner_stage *s) {
 return static_cast<OwnerFixture*>(p)->decoder.PrepareState(v,s);
}
extern "C" int xe_xma_owner_test_commit(void *p,xe_xma_owner_stage *s) {
 return static_cast<OwnerFixture*>(p)->decoder.CommitState(s);
}
extern "C" int xe_xma_owner_test_wrong_owner(xe_xma_owner_stage *s) {
 xe::apu::XmaContextNew other;return other.CommitState(s);
}
extern "C" void xe_xma_owner_test_discard(xe_xma_owner_stage *s) {
 xe::apu::XmaContextNew::DiscardState(s);
}
