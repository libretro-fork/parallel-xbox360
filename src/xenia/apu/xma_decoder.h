/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_DECODER_H_
#define XENIA_APU_XMA_DECODER_H_

#include <retro_atomic.h>
#include "libretro/core_pause_gate.h"
#include "libretro/core_xma_decoder_state.h"

#include "xenia/apu/xma_context.h"
#include "xenia/apu/xma_register_file.h"
#include "xenia/base/bit_map.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

struct XMA_CONTEXT_DATA;

class XmaDecoder {
 public:
  explicit XmaDecoder(cpu::Processor* processor);
  ~XmaDecoder();

  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  X_STATUS Setup(kernel::KernelState* kernel_state);
  void Shutdown();

  uint32_t context_array_ptr() const {
    return register_file_[XmaRegister::ContextArrayAddress];
  }

  uint32_t AllocateContext();
  void ReleaseContext(uint32_t guest_ptr);
  bool BlockOnContext(uint32_t guest_ptr, bool poll);

  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);

  bool is_paused() const { return xe_pause_gate_is_paused(pause_gate_.get()) != 0; }
  void Pause();
  /* Synchronous decoder callers must already be quiescent. */
  bool PauseForState(int64_t timeout_us);
  void Resume();

  // Exclusive state owner: guest/control callers must also be stopped.
  bool CaptureState(xe_xma_decoder_view* out) const;
  bool PrepareState(const xe_xma_decoder_view* view, xe_xma_decoder_stage* stage) const;
  bool CanCommitState(const xe_xma_decoder_stage* stage) const;
  bool CommitState(xe_xma_decoder_stage* stage);
  static void DiscardState(xe_xma_decoder_stage* stage);

  void SignalWork() {
    if (work_event_) {
      work_event_->Set();
    }
  }

 protected:
  int GetContextId(uint32_t guest_ptr);
  // Cold factory shared by Setup and state-only offline fixtures.
  bool InitializeContexts(uint32_t mode);

 private:
  bool StateQuiescent() const;
  bool StageStorageValid(const xe_xma_decoder_stage* stage) const;
  bool StateRegistersValid(const uint32_t* registers) const;
  void WorkerThreadMain();

  static uint32_t MMIOReadRegisterThunk(void* ppc_context, XmaDecoder* as,
                                        uint32_t addr) {
    return as->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(void* ppc_context, XmaDecoder* as,
                                     uint32_t addr, uint32_t value) {
    as->WriteRegister(addr, value);
  }

 protected:
  Memory* memory_ = nullptr;
  cpu::Processor* processor_ = nullptr;

  retro_atomic_int_t worker_running_{0};
  kernel::object_ref<kernel::XHostThread> worker_thread_;
  std::unique_ptr<xe::threading::Event> work_event_ = nullptr;

  std::unique_ptr<xe_pause_gate, decltype(&xe_pause_gate_free)> pause_gate_{
      nullptr, &xe_pause_gate_free};

  XmaRegisterFile register_file_;

  static const uint32_t kContextCount = XE_XMA_CONTEXTS;
  uint32_t context_mode_ = XE_XMA_MODE_UNSET;
  XmaContext* contexts_[kContextCount] = {};
  BitMap context_bitmap_;

  uint32_t context_data_first_ptr_ = 0;
  uint32_t context_data_last_ptr_ = 0;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_DECODER_H_
