#include "xenia/emulator.h"
#include "xenia/apu/nop/nop_audio_system.h"
#include "xenia/apu/xma_context_new.h"
#include "xenia/apu/xma_context_old.h"
#include "xenia/apu/xma_context_master.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "third_party/FFmpeg/libavcodec/avcodec.h"
#include "third_party/FFmpeg/libavutil/buffer.h"
}
DECLARE_bool(use_dedicated_xma_thread);
using xe::X_STATUS;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)

/* The extra reference observes ownership without instrumenting release code. */
template<class Context> class observed_context : public Context {
 public:
  AVBufferRef *retain_packet() { return av_buffer_ref(this->av_packet_->buf); }
};
template<class Context> static void check_packet_release() {
  auto *context = new observed_context<Context>();
  CHECK(context->Setup(0, nullptr, 0) == 0);
  AVBufferRef *retained = context->retain_packet();
  CHECK(retained && av_buffer_get_ref_count(retained) == 2);
  delete context;
  CHECK(av_buffer_get_ref_count(retained) == 1);
  av_buffer_unref(&retained);
}

int main() {
  xe::InitializeLogging("audio_state_test");
  xe::Profiler::Initialize();
  for (unsigned i = 0; i < 20; ++i) {
    check_packet_release<xe::apu::XmaContextNew>();
    check_packet_release<xe::apu::XmaContextOld>();
    check_packet_release<xe::apu::XmaContextMaster>();
  }
  for (unsigned mode = 0; mode < 2; ++mode) {
    cvars::use_dedicated_xma_thread = mode != 0;
    xe::Emulator emulator({}, "build/tests/state-runtime", "build/tests/state-runtime", "build/tests/state-runtime");
    CHECK(emulator.Setup(true, {}, {}, {}) == X_STATUS_SUCCESS);
    {
      xe::apu::nop::NopAudioSystem audio(emulator.processor());
      CHECK(audio.Setup(emulator.kernel_state()) == X_STATUS_SUCCESS);
      for (unsigned generation = 0; generation < 200; ++generation) {
        CHECK(audio.PauseForState(2000000));
        CHECK(audio.is_paused());
        audio.Resume();
      }
      CHECK(audio.PauseForState(2000000));
      audio.Shutdown();
    }
    emulator.Shutdown();
  }
  std::puts("PASS: all three XMA packet owners release buffers; audio/XMA idle workers pause/resume 200 times in both modes and close while paused");
  return 0;
}
