/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_AUDIO_SYSTEM_H_
#define XENIA_APU_AUDIO_SYSTEM_H_

#include "libretro/core_pause_gate.h"
#include "libretro/core_state_control.h"
#include "libretro/core_apu_clients.h"
#include <retro_atomic.h>
#include <mutex>

#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

class AudioDriver;
class XmaDecoder;

class AudioSystem {
 public:
  // TODO(gibbed): respect XAUDIO2_MAX_QUEUED_BUFFERS somehow (ie min(64,
  // XAUDIO2_MAX_QUEUED_BUFFERS))
  static constexpr size_t kMinimumQueuedFrames = 4;
  static constexpr size_t kMaximumQueuedFrames = 64;
  static constexpr uint32_t kAudioPumpInterval = 5333u;
  static constexpr uint32_t kAudioIntervalSlack = 400u;

  virtual ~AudioSystem();

  virtual std::string name() const = 0;

  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  XmaDecoder* xma_decoder() const { return xma_decoder_.get(); }

  virtual X_STATUS Setup(kernel::KernelState* kernel_state);
  virtual void Shutdown();
  // Stops the worker that runs the guest audio callbacks. Idempotent, and
  // Shutdown does it too.
  virtual void StopWorker();
  // Wake output submissions before dependent workers are joined.
  virtual void CancelPendingOutput() {}
  // Called after frontend consumption makes output capacity available.
  void NotifyOutputSpace() { pending_work_event_->Set(); }

  X_STATUS RegisterClient(uint32_t callback, uint32_t callback_arg,
                          size_t* out_index);
  void UnregisterClient(size_t index);
  void SubmitFrame(size_t index, float* samples);

  // Get performance statistics for a client
  struct ClientPerformance {
    uint32_t frames_submitted;
    uint32_t frames_processed;
    uint32_t frames_dropped;
  };
  bool GetClientPerformance(size_t index, ClientPerformance* out_perf);

  // Creates an independent, non-registered driver instance.
  virtual AudioDriver* CreateDriver(xe::threading::Semaphore* semaphore,
                                    uint32_t frequency, uint32_t channels,
                                    bool need_format_conversion) = 0;

  bool is_paused() const { return xe_pause_gate_is_paused(pause_gate_.get()) != 0; }
  void Pause();
  bool PauseForState(int64_t timeout_us);
  void Resume();

 protected:
  explicit AudioSystem(cpu::Processor* processor);

  virtual void Initialize();

  void WorkerThreadMain();

  virtual X_STATUS CreateDriver(size_t index,
                                const xe_apu_credit_sink* credits,
                                AudioDriver** out_driver) = 0;
  virtual void DestroyDriver(AudioDriver* driver) = 0;
  bool BeginClientCallback(size_t index,uint64_t expected,uint32_t* callback,
                           uint32_t* argument,uint64_t* lease);
  void EndClientCallback(size_t index,uint64_t lease);
  void UnregisterClientInternal(size_t index,bool self);
  void CollectRetiredClients();

  Memory* memory_ = nullptr;
  cpu::Processor* processor_ = nullptr;
  std::unique_ptr<XmaDecoder> xma_decoder_;
  uint32_t queued_frames_;

  retro_atomic_int_t worker_running_{0};
  kernel::object_ref<kernel::XHostThread> worker_thread_;

  // Binding metadata and SPSC submissions remain serialized. Guest callbacks
  // execute outside this lock; their lifetime uses generation-checked leases.
  std::mutex clients_mutex_;
  std::unique_ptr<xe_apu_clients,decltype(&xe_apu_clients_free)> client_lifetimes_{
      nullptr,&xe_apu_clients_free};
  static constexpr size_t kMaximumClientCount = XE_APU_CLIENT_COUNT;
  struct ClientSlot {
    AudioDriver* driver = nullptr;
    uint32_t callback = 0;
    uint32_t callback_arg = 0;
    uint32_t wrapped_callback_arg = 0;
    uint64_t next_pump_us = 0;
    retro_atomic_int_t frames_submitted{0};
    retro_atomic_int_t frames_processed{0};
    retro_atomic_int_t frames_dropped{0};
  };
  ClientSlot clients_[kMaximumClientCount];

  xe_state_control pause_control_{};
  void CleanupClient(size_t index,uint64_t token);
  void ClearClientSlot(size_t index);
  // Wakes the worker to re-scan clients on register, shutdown, and pause.
  std::unique_ptr<xe::threading::Event> pending_work_event_;

  std::unique_ptr<xe_pause_gate, decltype(&xe_pause_gate_free)> pause_gate_{
      nullptr, &xe_pause_gate_free};
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_AUDIO_SYSTEM_H_
