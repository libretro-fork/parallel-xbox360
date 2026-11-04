/* Actual APU worker and generated PPC import; no host audio device. */
#include "libretro/libretro_audio_driver.h"
#include "xenia/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/raw_module.h"
#include "xenia/cpu/function.h"
#include "xenia/kernel/kernel_state.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
using xe::X_STATUS;
static xe::apu::libretro::LibretroAudioSystem* audio;
static size_t indices[2], position[2];
static float packets[2][1536];
static unsigned char decoded_pcm[2097152];
static size_t decoded_frames;
static float sample(size_t frame,unsigned channel,unsigned multiplier) {
  if (decoded_frames) {
    size_t offset=(frame%decoded_frames)*4+channel*2;
    int value=(int)((unsigned)decoded_pcm[offset]*256+decoded_pcm[offset+1]);
    if (value>=32768) value-=65536;
    return float(value*(int)multiplier)/131072.0f;
  }
  return float(((frame%1024)*2+channel)*multiplier)/32768.0f;
}
static void render(xe::cpu::ppc::PPCContext* context,xe::kernel::KernelState* kernel) {
  unsigned stream=xe::load_and_swap<uint32_t>(kernel->memory()->TranslateVirtual(uint32_t(context->r[3])));
  unsigned i,ch;
  CHECK(stream<2);
  for(ch=0;ch<6;++ch) for(i=0;i<256;++i) {
    float value=ch<2?sample(position[stream]+i,ch,stream+1):0.0f;
    uint32_t bits;
    std::memcpy(&bits,&value,sizeof(bits));
    xe::store_and_swap<uint32_t>(&packets[stream][ch*256+i],bits);
  }
  audio->SubmitFrame(indices[stream],packets[stream]);
  position[stream]+=256;
}
int main() {
  const char* path=std::getenv("XMA_OUTPUT_REFERENCE");
  if (path) {
    FILE* file=std::fopen(path,"rb");
    size_t bytes;
    CHECK(file);
    bytes=std::fread(decoded_pcm,1,sizeof(decoded_pcm),file);
    CHECK(std::feof(file) && !std::ferror(file) && bytes && bytes%4==0);
    CHECK(std::fclose(file)==0);
    decoded_frames=bytes/4;
  }
  xe::InitializeLogging("audio_run_request_test");
  xe::Profiler::Initialize();
  xe::Emulator emulator({},"build/tests/state-runtime","build/tests/state-runtime","build/tests/state-runtime");
  CHECK(emulator.Setup(true,{},{},{})==X_STATUS_SUCCESS);
  emulator.processor()->EnableDynamicCode();
  uint32_t code=emulator.memory()->SystemHeapAlloc(4096);
  CHECK(code);
  xe::store_and_swap<uint32_t>(emulator.memory()->TranslateVirtual(code),0x44000042);
  xe::store_and_swap<uint32_t>(emulator.memory()->TranslateVirtual(code+4),0x4E800020);
  auto module=std::make_unique<xe::cpu::RawModule>(emulator.processor());
  module->SetAddressRange(code,8);
  xe::cpu::Function* function;
  module->DeclareFunction(code,&function);
  static_cast<xe::cpu::GuestFunction*>(function)->SetupExtern(render);
  function->set_status(xe::cpu::Symbol::Status::kDeclared);
  CHECK(emulator.processor()->AddModule(std::move(module)));
  xe_audio_registry* registry=xe_audio_registry_new(2);
  CHECK(registry);
  {
    xe::apu::libretro::LibretroAudioSystem system(emulator.processor(),registry);
    audio=&system;
    CHECK(system.Setup(emulator.kernel_state())==X_STATUS_SUCCESS);
    for(unsigned i=0;i<2;++i) CHECK(system.RegisterClient(code,i,&indices[i])==X_STATUS_SUCCESS);
    float output[960*2];
    size_t frame=0;
    for(unsigned iteration=0;iteration<300;++iteration) {
      size_t frames=iteration%3==0?800:(iteration%3==1?960:400);
      CHECK(xe_audio_registry_mix(registry,output,frames*2)==frames*2);
      for(size_t i=0;i<frames;++i) for(unsigned ch=0;ch<2;++ch)
        CHECK(output[i*2+ch]==sample(frame+i,ch,1)+sample(frame+i,ch,2));
      frame+=frames;

    }
    system.StopWorker();
    for(unsigned i=0;i<2;++i) CHECK(position[i]==((frame+255)/256)*256);
    system.Shutdown();
  }
  xe_audio_registry_free(registry);
  emulator.Shutdown();
  std::puts("PASS: actual APU worker/PPC callbacks; 300 frontend batches with exact mixed samples and native quantum callback counts; clean shutdown");
  return 0;
}