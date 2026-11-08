/* Actual AudioSystem ownership/heap ABI; no Setup, guest Execute or device. */
#include "xenia/apu/audio_system.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <new>
using xe::X_STATUS;
struct Observations {unsigned created=0,destroyed=0,submitted=0;};
class ObservedDriver:public xe::apu::AudioDriver {
  Observations *observations_;
  xe_apu_credit_sink credits_;
 public:
  ObservedDriver(Observations *p,const xe_apu_credit_sink *credits):observations_(p),credits_(*credits) {++p->created;}
  ~ObservedDriver() override {++observations_->destroyed;}
  bool Initialize() override {return true;}
  void Shutdown() override {}
  bool SubmitInterleavedFrames(const float *,uint32_t) override {return false;}
  void SubmitFrame(float *) override {++observations_->submitted;xe_apu_clients_return_credit(&credits_);}
  void Pause() override {}
  void Resume() override {}
  void SetVolume(float) override {}
};
class ObservedAudio:public xe::apu::AudioSystem {
 public:
  Observations observations;
  unsigned mode=0;
  explicit ObservedAudio(xe::cpu::Processor *processor):AudioSystem(processor) {}
  ~ObservedAudio() override {Shutdown();}
  std::string name() const override {return "APU fixture";}
  xe::apu::AudioDriver *CreateDriver(xe::threading::Semaphore *,uint32_t,uint32_t,bool) override {return nullptr;}
  xe::X_STATUS CreateDriver(size_t,const xe_apu_credit_sink *credits,xe::apu::AudioDriver **out) override {
    if(mode==1) return X_STATUS_UNSUCCESSFUL;
    if(mode==2) throw std::bad_alloc();
    if(mode==3) return X_STATUS_SUCCESS;
    *out=new ObservedDriver(&observations,credits);return X_STATUS_SUCCESS;
  }
  void DestroyDriver(xe::apu::AudioDriver *driver) override {delete driver;}
  bool begin(unsigned i,uint64_t expected,uint32_t *callback,uint32_t *argument,uint64_t *lease) {
    return BeginClientCallback(i,expected,callback,argument,lease);
  }
  void end(unsigned i,uint64_t lease) {EndClientCallback(i,lease);}
  void retire_self(unsigned i) {UnregisterClientInternal(i,true);}
  void collect() {CollectRetiredClients();}
  uint64_t token(unsigned i) {uint64_t t=UINT64_MAX;xe_apu_clients_active(client_lifetimes_.get(),i,&t);return t;}
  unsigned credits(unsigned i,uint64_t token) {unsigned n=999;xe_apu_clients_credits(client_lifetimes_.get(),i,token,&n);return n;}
  int stale_return(unsigned i,uint64_t token) {xe_apu_credit_sink sink={client_lifetimes_.get(),token,i};return xe_apu_clients_return_credit(&sink);}
};
struct SystemFixture {
  xe::Memory memory;
  xe::cpu::Processor processor;
  ObservedAudio audio;
  SystemFixture():processor(&memory,nullptr),audio(&processor) {}
};
extern "C" void *xe_apu_system_new(void) {
  static bool logging=false;
  if(!logging) {xe::InitializeLogging("apu_client_system_test");xe::Profiler::Initialize();logging=true;}
  SystemFixture *f=new(std::nothrow) SystemFixture;
  if(f && !f->memory.Initialize()) {delete f;return nullptr;}
  return f;
}
extern "C" void xe_apu_system_free(void *p) {delete static_cast<SystemFixture*>(p);}
extern "C" int xe_apu_system_register(void *p,uint32_t value,unsigned *out) {
  size_t index=99;
  if(static_cast<SystemFixture*>(p)->audio.RegisterClient(0x80000010,value,&index)!=X_STATUS_SUCCESS) return 0;
  *out=(unsigned)index;return 1;
}
extern "C" void xe_apu_system_unregister(void *p,unsigned i) {static_cast<SystemFixture*>(p)->audio.UnregisterClient(i);}
extern "C" void xe_apu_system_self(void *p,unsigned i) {static_cast<SystemFixture*>(p)->audio.retire_self(i);}
extern "C" void xe_apu_system_collect(void *p) {static_cast<SystemFixture*>(p)->audio.collect();}
extern "C" uint64_t xe_apu_system_token(void *p,unsigned i) {return static_cast<SystemFixture*>(p)->audio.token(i);}
extern "C" int xe_apu_system_begin(void *p,unsigned i,uint64_t expected,uint32_t *callback,uint32_t *argument,uint64_t *lease) {
  return static_cast<SystemFixture*>(p)->audio.begin(i,expected,callback,argument,lease);
}
extern "C" void xe_apu_system_end(void *p,unsigned i,uint64_t lease) {static_cast<SystemFixture*>(p)->audio.end(i,lease);}
extern "C" unsigned xe_apu_system_credits(void *p,unsigned i,uint64_t token) {return static_cast<SystemFixture*>(p)->audio.credits(i,token);}
extern "C" int xe_apu_system_stale(void *p,unsigned i,uint64_t token) {return static_cast<SystemFixture*>(p)->audio.stale_return(i,token);}
extern "C" void xe_apu_system_submit(void *p,unsigned i,float *samples) {static_cast<SystemFixture*>(p)->audio.SubmitFrame(i,samples);}
extern "C" int xe_apu_system_stats(void *p,unsigned i,uint32_t *values) {
  xe::apu::AudioSystem::ClientPerformance v;
  if(!static_cast<SystemFixture*>(p)->audio.GetClientPerformance(i,&v)) return 0;
  values[0]=v.frames_submitted;values[1]=v.frames_processed;values[2]=v.frames_dropped;return 1;
}
extern "C" void xe_apu_system_observed(void *p,unsigned *values) {
  const Observations& v=static_cast<SystemFixture*>(p)->audio.observations;
  values[0]=v.created;values[1]=v.destroyed;values[2]=v.submitted;
}
extern "C" void xe_apu_system_mode(void *p,unsigned mode) {static_cast<SystemFixture*>(p)->audio.mode=mode;}
extern "C" uint32_t xe_apu_system_pages(void *p) {return static_cast<SystemFixture*>(p)->memory.LookupHeapByType(false,4096)->unreserved_page_count();}
extern "C" int xe_apu_system_unallocated(void *p,uint32_t address) {
  SystemFixture *f=static_cast<SystemFixture*>(p);return f->memory.LookupHeap(address)->IsRangeUnallocated(address,4);
}
extern "C" const void *xe_apu_system_argument(void *p,uint32_t address) {return static_cast<SystemFixture*>(p)->memory.TranslateVirtual(address);}
extern "C" void *xe_apu_legacy_credit_new(void) {return xe::threading::Semaphore::Create(1,1).release();}
extern "C" void xe_apu_legacy_credit_free(void *p) {delete static_cast<xe::threading::Semaphore*>(p);}
extern "C" int xe_apu_legacy_credit_step(void *p) {
  xe::threading::Semaphore *semaphore=static_cast<xe::threading::Semaphore*>(p);
  return xe::threading::Wait(semaphore,false,std::chrono::milliseconds(0))==xe::threading::WaitResult::kSuccess &&
      semaphore->Release(1,nullptr);
}
