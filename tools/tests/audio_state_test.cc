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
#include <cstring>
extern "C" {
#include "third_party/FFmpeg/libavcodec/avcodec.h"
#include "third_party/FFmpeg/libavutil/buffer.h"
}
DECLARE_bool(use_dedicated_xma_thread);
using xe::X_STATUS;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)

namespace xe { namespace apu {
struct XmaContextWritebackTest {
  static bool run() {
    XmaContextNew decoder;
    uint8_t storage[sizeof(XMA_CONTEXT_DATA)] = {};
    unsigned cursor;
    XMA_CONTEXT_DATA initial(storage), decoded(storage), guest(storage);
    initial.output_buffer_read_offset=0;
    initial.output_buffer_write_offset=1;
    initial.input_buffer_0_valid=1;
    initial.output_buffer_valid=1;
    decoded=initial;
    decoded.output_buffer_write_offset=7;
    decoded.input_buffer_0_valid=0;
    for (cursor=0;cursor<32;++cursor) {
      XMA_CONTEXT_DATA actual(storage);
      guest=initial;
      guest.output_buffer_read_offset=cursor;
      guest.input_buffer_1_packet_count=123;
      guest.Store(storage);
      decoder.StoreContextMerged(decoded,initial,storage);
      actual=XMA_CONTEXT_DATA(storage);
      if (actual.output_buffer_read_offset!=cursor ||
          actual.input_buffer_1_packet_count!=123 ||
          actual.output_buffer_write_offset!=7 || actual.input_buffer_0_valid)
        return false;
    }
    return true;
  }
};
}}

/* The extra reference observes ownership without instrumenting release code. */
template<class Context> class observed_context : public Context {
 public:
  AVBufferRef *retain_packet() { return av_buffer_ref(this->av_packet_->buf); }
  AVPacket *packet() { return this->av_packet_; }
  uint8_t *assembly() { return this->xma_frame_; }
  bool payload_size(uint32_t size) { return this->SetFramePacketSize(size); }
  uint32_t capacity() { return this->kFrameCapacity; }
  AVCodecContext *codec() { return this->av_context_; }
  AVFrame *frame() { return this->av_frame_; }
};

static void put_bits(uint8_t *data, uint32_t *position, uint32_t value,
                     unsigned count) {
  unsigned i;
  for (i = count; i; --i) {
    if ((value >> (i - 1)) & 1)
      data[*position / 8] |= (uint8_t)(1u << (7 - *position % 8));
    ++*position;
  }
}

/* Full-frame silent mono/stereo XMA: no coefficients, DRC=0, no trim.
 * Exercise the actual decoder with every packet-prefix alignment. */
template<class Context> static void check_decode() {
  const int rates[] = {24000, 32000, 44100, 48000};
  for (unsigned rate = 0; rate < 4; ++rate) for (unsigned channels = 1; channels <= 2; ++channels) {
    observed_context<Context> context;
    CHECK(context.Setup(0, nullptr, 0) == 0);
    context.codec()->sample_rate = rates[rate];
    av_channel_layout_default(&context.codec()->ch_layout, channels);
    CHECK(avcodec_open2(context.codec(), avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES), nullptr) == 0);
    for (unsigned padding = 0; padding < 8; ++padding) {
      xe_xma_packet_span packet;
      const uint32_t bits = channels == 1 ? 31 : 36;
      CHECK(xe_xma_packet_layout(bits, padding, context.capacity(), &packet));
      std::memset(context.assembly(), 0, context.capacity());
      CHECK(context.payload_size(packet.size));
      context.assembly()[0] = packet.header;
      uint32_t pos = 8 + padding;
      put_bits(context.assembly(), &pos, bits, 15);
      put_bits(context.assembly(), &pos, 1, 1);  // shared tiling
      put_bits(context.assembly(), &pos, 0, 1);  // full-length subframe
      if (channels == 2) put_bits(context.assembly(), &pos, 0, 1);  // no postprocess
      put_bits(context.assembly(), &pos, 0, 8);  // DRC
      put_bits(context.assembly(), &pos, 0, 1);  // no trim
      put_bits(context.assembly(), &pos, 0, 1);  // no extended subframe header
      put_bits(context.assembly(), &pos, 0, 1);  // reserved
      if (channels == 2) {
        put_bits(context.assembly(), &pos, 0, 1);
        put_bits(context.assembly(), &pos, 2, 2);  // uncoupled stereo
      }
      put_bits(context.assembly(), &pos, 0, channels);  // no coefficients
      put_bits(context.assembly(), &pos, 0, 2);  // frame padding and trailer
      CHECK(pos == 8 + padding + bits);
      CHECK(avcodec_send_packet(context.codec(), context.packet()) == 0);
      CHECK(avcodec_receive_frame(context.codec(), context.frame()) == 0);
      CHECK(context.frame()->nb_samples == 512);
      for (unsigned ch = 0; ch < channels; ++ch) for (unsigned i = 0; i < 512; ++i)
        CHECK(reinterpret_cast<const float*>(context.frame()->extended_data[ch])[i] == 0.0f);
      CHECK(context.packet()->data == context.assembly());
    }
  }
}
template<class Context> static void check_packet_release() {
  auto *context = new observed_context<Context>();
  CHECK(context->Setup(0, nullptr, 0) == 0);
  AVPacket *packet = context->packet();
  CHECK(packet && packet->data == packet->buf->data && packet->data == context->assembly());
  CHECK(packet->buf->size == context->capacity() + AV_INPUT_BUFFER_PADDING_SIZE);
  CHECK(packet->buf->size < 8192);
  uint8_t *owned = packet->data;
  std::memset(owned, 0xA5, packet->buf->size);
  CHECK(context->payload_size(65));
  for (unsigned i = 0; i < 65; ++i) CHECK(owned[i] == 0xA5);
  for (unsigned i = 65; i < 65 + AV_INPUT_BUFFER_PADDING_SIZE; ++i) CHECK(!owned[i]);
  CHECK(owned[65 + AV_INPUT_BUFFER_PADDING_SIZE] == 0xA5);
  CHECK(context->payload_size(context->capacity()));
  for (unsigned i = 0; i < AV_INPUT_BUFFER_PADDING_SIZE; ++i)
    CHECK(!owned[context->capacity() + i]);
  CHECK(!context->payload_size(context->capacity() + 1));
  CHECK(!context->payload_size(0));
  for (unsigned i = 1; i < 10000; ++i) {
    CHECK(context->payload_size(i % context->capacity() + 1));
    CHECK(packet->data == owned && packet->buf->data == owned);
  }
  CHECK(context->payload_size(16));
  owned[1] = 0x5A;
  AVPacket *clone = av_packet_alloc();
  CHECK(clone && av_packet_ref(clone, packet) == 0 && clone->data == owned);
  AVBufferRef *retained = context->retain_packet();
  CHECK(retained && av_buffer_get_ref_count(retained) == 3);
  delete context;
  CHECK(av_buffer_get_ref_count(retained) == 2 && clone->data[1] == 0x5A);
  av_packet_free(&clone);
  CHECK(av_buffer_get_ref_count(retained) == 1 && retained->data[1] == 0x5A);
  av_buffer_unref(&retained);
}

int main() {
  if (!xe::apu::XmaContextWritebackTest::run()) {
    std::puts("FAIL: XMA writeback lost guest PCM read cursor"); return 1;
  }
  std::puts("PASS: XMA writeback preserves all 32 guest read cursors and fresh input metadata");
  xe::InitializeLogging("audio_state_test");
  xe::Profiler::Initialize();
  check_decode<xe::apu::XmaContextNew>();
  check_decode<xe::apu::XmaContextOld>();
  check_decode<xe::apu::XmaContextMaster>();
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
  std::puts("PASS: all three XMA packet owners assemble in reusable padded backing and retained views survive deletion; audio/XMA idle workers pause/resume and close while paused");
  return 0;
}
