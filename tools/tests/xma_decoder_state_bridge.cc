/* Native cold object setup only. Processor::Setup/backend/guest code never run. */
#include "xenia/apu/xma_decoder.h"
#include "xenia/apu/xma_context_new.h"
#include "xenia/cpu/processor.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <new>
#include <limits.h>
#include <stdlib.h>
static size_t xma_alloc_limit = SIZE_MAX, xma_fail_after = SIZE_MAX;
static unsigned xma_alloc_calls, xma_live_allocations;
extern "C" void *xe_xma_test_calloc(size_t n,size_t size) {
 void *p;
 ++xma_alloc_calls;
 if (xma_alloc_calls>xma_fail_after || (size && n>xma_alloc_limit/size)) return 0;
 p=calloc(n,size);
 if(p) ++xma_live_allocations;
 return p;
}
extern "C" void xe_xma_test_free(void *p) {
 if(p) {--xma_live_allocations;free(p);}
}
extern "C" unsigned xe_xma_decoder_test_alloc_hits(void) {return xma_alloc_calls;}
extern "C" unsigned xe_xma_decoder_test_live_allocations(void) {return xma_live_allocations;}
extern "C" void xe_xma_decoder_test_fail_after(size_t n) {xma_alloc_calls=0;xma_fail_after=n;}
using xe::X_STATUS;
namespace xe {namespace apu {
struct XmaContextStreamTest {
 static void *raw(XmaContextNew *context) {return context->raw_frame_;}
 static const void *codec(XmaContextNew *context) {return context->native_.codec;}
 static int configure(XmaContextNew *context,unsigned rate,unsigned channels) {
  return context->PrepareDecoder(rate,channels==2);
 }
};
}}
class ColdDecoder : public xe::apu::XmaDecoder {
 public:
  explicit ColdDecoder(xe::cpu::Processor *p):XmaDecoder(p) {}
  bool Init(uint32_t mode) {
    context_data_first_ptr_=memory()->SystemHeapAlloc(320*64,256,xe::kSystemHeapPhysical);
    if(!context_data_first_ptr_) return false;
    context_data_last_ptr_=context_data_first_ptr_+320*64-1;
    register_file_[xe::apu::XmaRegister::ContextArrayAddress]=memory()->GetPhysicalAddress(context_data_first_ptr_);
    return InitializeContexts(mode);
  }
  void pending(int value) {
    retro_atomic_store_release_int(&worker_running_,value);
    xe_pause_gate_set(pause_gate_.get(),value);
  }
  void *gate() {return pause_gate_.get();}
  bool rebuild() {Shutdown();return Init(XE_XMA_MODE_RAW);}
  int stream(const void *packet,unsigned channels,bool execute=true) {
    contexts_[0]->Clear();
    uint32_t input=memory()->SystemHeapAlloc(4096,4096,xe::kSystemHeapPhysical);
    uint32_t output=memory()->SystemHeapAlloc(4096,4096,xe::kSystemHeapPhysical);
    if(!input || !output) return 0;
    uint32_t physical=memory()->GetPhysicalAddress(input);
    memcpy(memory()->TranslatePhysical(physical),packet,2048);
    void *context=memory()->TranslateVirtual(context_data_first_ptr_);
    memset(context,0,64);
    xe::apu::XMA_CONTEXT_DATA data(context);
    data.input_buffer_0_ptr=physical;data.input_buffer_0_packet_count=1;
    data.input_buffer_0_valid=1;data.input_buffer_read_offset=32;
    data.output_buffer_ptr=memory()->GetPhysicalAddress(output);
    data.output_buffer_block_count=3;data.output_buffer_valid=1;
    data.sample_rate=3;data.is_stereo=channels==2;data.subframe_decode_count=1;data.Store(context);
    if(!execute) return 1;
    contexts_[0]->Enable();return contexts_[0]->Work();
  }
  unsigned legacy(const void *packet,unsigned channels,const void **pcm) {
    if(!stream(packet,channels,false)) return 0;
    void *context=memory()->TranslateVirtual(context_data_first_ptr_);
    xe::apu::XMA_CONTEXT_DATA data(context);
    data.output_buffer_block_count=16;data.Store(context);
    *pcm=memory()->TranslatePhysical(data.output_buffer_ptr);
    memset(const_cast<void*>(*pcm),0xa5,4096);
    contexts_[0]->Enable();if(!contexts_[0]->Work()) return 0;
    xe::apu::XMA_CONTEXT_DATA after(context);
    return after.output_buffer_write_offset*256;
  }
  void *raw() {return xe::apu::XmaContextStreamTest::raw(static_cast<xe::apu::XmaContextNew*>(contexts_[0]));}
  const void *drain(unsigned stereo) {
    void *context=memory()->TranslateVirtual(context_data_first_ptr_);
    xe::apu::XMA_CONTEXT_DATA data(context);
    data.is_stereo=stereo;data.output_buffer_read_offset=1;data.output_buffer_valid=1;
    data.Store(context);contexts_[0]->Enable();
    if(!contexts_[0]->Work()) return nullptr;
    return memory()->TranslatePhysical(data.output_buffer_ptr);
  }
  const void *registers() const {return register_file_.values;}
  void inconsistent(unsigned index) {contexts_[index]->set_is_allocated(!contexts_[index]->is_allocated());}
  const void *codec(unsigned index) {
    return xe::apu::XmaContextStreamTest::codec(static_cast<xe::apu::XmaContextNew*>(contexts_[index]));
  }
  bool rotate(unsigned index) {
    xe_xma_owner_view view={};xe_xma_owner_stage stage={};
    xe::apu::XmaContextNew *context=static_cast<xe::apu::XmaContextNew*>(contexts_[index]);
    if(!context->CaptureState(&view) || !context->PrepareState(&view,&stage)) return false;
    if(context->CommitState(&stage)) return true;
    context->DiscardState(&stage);return false;
  }
  int configure(unsigned rate,unsigned channels) {
    return xe::apu::XmaContextStreamTest::configure(static_cast<xe::apu::XmaContextNew*>(contexts_[0]),rate,channels);
  }
  const void *guest() {return memory()->TranslateVirtual(context_data_first_ptr_);}
  bool work() {contexts_[0]->Enable();return contexts_[0]->Work();}
  void cursor(unsigned offset,unsigned loops) {
    void *context=memory()->TranslateVirtual(context_data_first_ptr_);
    xe::apu::XMA_CONTEXT_DATA data(context);
    data.input_buffer_read_offset=offset;data.loop_count=loops;
    data.loop_start=data.loop_end=32;data.Store(context);
  }
  void current(unsigned buffer) {
    void *context=memory()->TranslateVirtual(context_data_first_ptr_);
    xe::apu::XMA_CONTEXT_DATA data(context);data.current_buffer=buffer;data.Store(context);
  }
};
struct DecoderFixture {
 xe::Memory memory;
 xe::cpu::Processor processor;
 ColdDecoder decoder;
 DecoderFixture():processor(&memory,nullptr),decoder(&processor) {}
};
extern "C" void *xe_xma_decoder_test_new(unsigned mode) {
 static bool logging=false;
 if(!logging) {xe::InitializeLogging("xma_decoder_state_test");xe::Profiler::Initialize();logging=true;}
 DecoderFixture *f=new(std::nothrow) DecoderFixture;
 if(!f) return nullptr;
 if(!f->memory.Initialize() || !f->decoder.Init(mode)) {delete f;return nullptr;}
 return f;
}
extern "C" void xe_xma_decoder_test_free(void *p) {delete static_cast<DecoderFixture*>(p);}
extern "C" int xe_xma_decoder_test_capture(void *p,xe_xma_decoder_view *v) {
 return static_cast<DecoderFixture*>(p)->decoder.CaptureState(v);
}
extern "C" uint32_t xe_xma_decoder_test_allocate(void *p) {
 return static_cast<DecoderFixture*>(p)->decoder.AllocateContext();
}
extern "C" void xe_xma_decoder_test_release(void *p,uint32_t address) {
 static_cast<DecoderFixture*>(p)->decoder.ReleaseContext(address);
}
extern "C" const void *xe_xma_decoder_test_registers(void *p) {
 return static_cast<DecoderFixture*>(p)->decoder.registers();
}
extern "C" void xe_xma_decoder_test_inconsistent(void *p,unsigned index) {
 static_cast<DecoderFixture*>(p)->decoder.inconsistent(index);
}

extern "C" void xe_xma_decoder_test_pending(void *p,int value) {
 static_cast<DecoderFixture*>(p)->decoder.pending(value);
}
extern "C" void *xe_xma_decoder_test_gate(void *p) {
 return static_cast<DecoderFixture*>(p)->decoder.gate();
}
extern "C" int xe_xma_decoder_test_rebuild(void *p) {
 return static_cast<DecoderFixture*>(p)->decoder.rebuild();
}

extern "C" int xe_xma_decoder_test_duplicate_setup(void *p) {
 return static_cast<DecoderFixture*>(p)->decoder.Setup(nullptr)==X_STATUS_UNSUCCESSFUL;
}

extern "C" int xe_xma_decoder_test_stream(void *p,const void *packet,unsigned channels) {
 return static_cast<DecoderFixture*>(p)->decoder.stream(packet,channels);
}

extern "C" void *xe_xma_decoder_test_raw(void *p) {return static_cast<DecoderFixture*>(p)->decoder.raw();}
extern "C" const void *xe_xma_decoder_test_drain(void *p,unsigned stereo) {return static_cast<DecoderFixture*>(p)->decoder.drain(stereo);}
extern "C" int xe_xma_decoder_test_prepare(void *p,const xe_xma_decoder_view *v,xe_xma_decoder_stage *s) {
 return static_cast<DecoderFixture*>(p)->decoder.PrepareState(v,s);
}
extern "C" int xe_xma_decoder_test_ready(void *p,const xe_xma_decoder_stage *s) {
 return static_cast<DecoderFixture*>(p)->decoder.CanCommitState(s);
}
extern "C" int xe_xma_decoder_test_commit(void *p,xe_xma_decoder_stage *s) {
 return static_cast<DecoderFixture*>(p)->decoder.CommitState(s);
}
extern "C" void xe_xma_decoder_test_discard(xe_xma_decoder_stage *s) {xe::apu::XmaDecoder::DiscardState(s);}
extern "C" const void *xe_xma_decoder_test_codec(void *p,unsigned i) {return static_cast<DecoderFixture*>(p)->decoder.codec(i);}
extern "C" int xe_xma_decoder_test_rotate(void *p,unsigned i) {return static_cast<DecoderFixture*>(p)->decoder.rotate(i);}
extern "C" int xe_xma_decoder_test_configure(void *p,unsigned r,unsigned c) {return static_cast<DecoderFixture*>(p)->decoder.configure(r,c);}
extern "C" void xe_xma_decoder_test_alloc_limit(size_t n) {xma_alloc_limit=n;}
extern "C" int xe_xma_decoder_test_load(void *p,const void *b,unsigned c) {return static_cast<DecoderFixture*>(p)->decoder.stream(b,c,false);}
extern "C" const void *xe_xma_decoder_test_guest(void *p) {return static_cast<DecoderFixture*>(p)->decoder.guest();}
extern "C" int xe_xma_decoder_test_work(void *p) {return static_cast<DecoderFixture*>(p)->decoder.work();}
extern "C" void xe_xma_decoder_test_cursor(void *p,unsigned o,unsigned l) {static_cast<DecoderFixture*>(p)->decoder.cursor(o,l);}
extern "C" void xe_xma_decoder_test_current(void *p,unsigned b) {static_cast<DecoderFixture*>(p)->decoder.current(b);}

extern "C" unsigned xe_xma_decoder_test_legacy(void *p,const void *packet,unsigned channels,const void **pcm) {
 return static_cast<DecoderFixture*>(p)->decoder.legacy(packet,channels,pcm);
}
