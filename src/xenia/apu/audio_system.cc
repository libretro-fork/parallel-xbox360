/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_system.h"
#include <limits>


#include "xenia/apu/apu_flags.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/kernel_state.h"

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// For ease of implementation, most audio related processing is handled in
// AudioSystem, and the functions here call off to it.
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal AudioSystem handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc

DEFINE_uint32(apu_max_queued_frames, 8,
              "Allows changing max buffered audio frames to reduce audio "
              "delay. Lowering this value might cause performance issues. "
              "Value range: [4-64]",
              "APU");
UPDATE_from_uint32(apu_max_queued_frames, 2024, 8, 31, 20, 64);

namespace xe {
namespace apu {

AudioSystem::AudioSystem(cpu::Processor* processor)
    : memory_(processor->memory()),
      processor_(processor),
      worker_running_(0) {
  pause_gate_.reset(xe_pause_gate_new());
  if (!pause_gate_) throw std::bad_alloc();
  queued_frames_ = std::clamp(cvars::apu_max_queued_frames,
                              static_cast<uint32_t>(kMinimumQueuedFrames),
                              static_cast<uint32_t>(kMaximumQueuedFrames));

  client_lifetimes_.reset(xe_apu_clients_new(queued_frames_));
  if (!client_lifetimes_) throw std::bad_alloc();
  pending_work_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  if (!pending_work_event_) throw std::bad_alloc();

  xma_decoder_ = std::make_unique<xe::apu::XmaDecoder>(processor_);
  static const xe_state_stage stages[] = {
    {[](void* p) noexcept -> int {return static_cast<AudioSystem*>(p)->is_paused();},
     [](void* p,int64_t timeout) noexcept -> int {
       auto* self=static_cast<AudioSystem*>(p);
       try {
         xe_pause_gate_set(self->pause_gate_.get(),1);
         if(self->pending_work_event_) self->pending_work_event_->Set();
         return !retro_atomic_load_acquire_int(&self->worker_running_) ||
             xe_pause_gate_wait_workers(self->pause_gate_.get(),1,timeout);
       } catch(...) {return 0;}
     },
     [](void* p) noexcept {xe_pause_gate_set(static_cast<AudioSystem*>(p)->pause_gate_.get(),0);}},
    {[](void* p) noexcept -> int {auto* xma=static_cast<AudioSystem*>(p)->xma_decoder_.get();return xma && xma->is_paused();},
     [](void* p,int64_t timeout) noexcept -> int {auto* xma=static_cast<AudioSystem*>(p)->xma_decoder_.get();try {return !xma || xma->PauseForState(timeout);} catch(...) {return 0;}},
     [](void* p) noexcept {auto* xma=static_cast<AudioSystem*>(p)->xma_decoder_.get();if(xma) xma->Resume();}}
  };
  if(!xe_state_control_init(&pause_control_,stages,2,this,nullptr)) throw std::bad_alloc();



}

AudioSystem::~AudioSystem() {
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }
}

X_STATUS AudioSystem::Setup(kernel::KernelState* kernel_state) {
  X_STATUS result = xma_decoder_->Setup(kernel_state);
  if (result) {
    return result;
  }

  retro_atomic_store_release_int(&worker_running_, 1);
  worker_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          kernel_state, 128 * 1024, 0,
          [this]() {
            WorkerThreadMain();
            return 0;
          },
          kernel_state->GetSystemProcess()));
  // As we run audio callbacks the debugger must be able to suspend us.
  worker_thread_->set_can_debugger_suspend(true);
  worker_thread_->set_name("Audio Worker");
  worker_thread_->Create();
  // Set high priority for this thread for better pacing.
  worker_thread_->SetPriority(24);
  return X_STATUS_SUCCESS;
}

void AudioSystem::WorkerThreadMain() {
  // Initialize driver and ringbuffer.
  Initialize();

  // Compatibility baseline: restore the pre-446c545f callback schedule while
  // guest time is still host-driven. This is not a hardware mixer clock.
  while (retro_atomic_load_acquire_int(&worker_running_)) {
    if (is_paused()) {
      xe_pause_gate_wait_index(pause_gate_.get(), 0);
      continue;
    }
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    size_t client_index = kMaximumClientCount;
    uint64_t selected_token = 0;
    uint64_t earliest_pump_us = std::numeric_limits<uint64_t>::max();
    {
      std::lock_guard<std::mutex> clients_lock(clients_mutex_);

      for (size_t i = 0; i < kMaximumClientCount; ++i) {
        uint64_t token;
        if (!xe_apu_clients_active(client_lifetimes_.get(), (unsigned)i, &token) ||
            clients_[i].next_pump_us >= earliest_pump_us) {
          continue;
        }
        earliest_pump_us = clients_[i].next_pump_us;
        client_index = i;
        selected_token = token;
      }

      if (client_index != kMaximumClientCount) {
        const double scalar = xe::Clock::guest_time_scalar();
        const uint64_t min_us =
            scalar > 0.0 ? static_cast<uint64_t>(kAudioPumpInterval / scalar)
                         : kAudioPumpInterval;
        // Counted from the deadline, so a late pump is made up and the host
        // queue absorbs the stall, until it falls further behind than the
        // queue holds.
        uint64_t next_pump_us = earliest_pump_us + min_us;
        if (now > earliest_pump_us + min_us * queued_frames_) {
          next_pump_us = now + min_us;
        }
        clients_[client_index].next_pump_us = next_pump_us;
      }
    }

    // No clients yet: park until one registers or we're told to stop.
    if (client_index == kMaximumClientCount) {
      xe::threading::Wait(pending_work_event_.get(), true);

      continue;
    }

    // Pace to kAudioIntervalSlack ahead of the deadline.
    const uint64_t wake_target_us = earliest_pump_us > kAudioIntervalSlack
                                        ? earliest_pump_us - kAudioIntervalSlack
                                        : 0;
    if (wake_target_us > now) {
      const std::chrono::milliseconds timeout((wake_target_us - now) / 1000);
      auto result =
          xe::threading::Wait(pending_work_event_.get(), true, timeout);
      if (result == xe::threading::WaitResult::kSuccess) {

        continue;
      }

      const uint64_t now_precise = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());
      if (wake_target_us > now_precise) {
        xe::threading::NanoSleepPrecise((wake_target_us - now_precise) * 1000);
      }
    }

    if (is_paused() || !retro_atomic_load_acquire_int(&worker_running_)) continue;

    uint32_t client_callback = 0;
    uint32_t client_callback_arg = 0;
    uint64_t lease;
    if (!BeginClientCallback(client_index, selected_token, &client_callback,
                             &client_callback_arg, &lease)) continue;
    try {
      if (client_callback) {
        SCOPE_profile_cpu_i("apu", "xe::apu::AudioSystem->client_callback");
        uint64_t args[] = {client_callback_arg};
        processor_->Execute(worker_thread_->thread_state(), client_callback, args,
                            xe::countof(args));
      }
    } catch (...) {
      EndClientCallback(client_index, lease);
      retro_atomic_store_release_int(&worker_running_, 0);
      throw;
    }
    EndClientCallback(client_index, lease);
  }
  retro_atomic_store_release_int(&worker_running_, 0);

  // TODO(benvanik): call module API to kill?
}

void AudioSystem::Initialize() {}

void AudioSystem::StopWorker() {
  xe_state_control_stop(&pause_control_);
  retro_atomic_store_release_int(&worker_running_, 0);
  pending_work_event_->Set();
  // A paused worker waits for the resume.
  xe_pause_gate_stop(pause_gate_.get());
  if (worker_thread_) {
    worker_thread_->Wait(0, 0, 0, nullptr);
    worker_thread_.reset();
  }
}

void AudioSystem::Shutdown() {
  size_t i;
  StopWorker();
  for (i = 0; i < kMaximumClientCount; ++i) UnregisterClient(i);
}

void AudioSystem::ClearClientSlot(size_t index) {
  ClientSlot& client = clients_[index];
  client.driver = nullptr;
  client.callback = client.callback_arg = client.wrapped_callback_arg = 0;
  client.next_pump_us = 0;
  retro_atomic_store_relaxed_int(&client.frames_submitted, 0);
  retro_atomic_store_relaxed_int(&client.frames_processed, 0);
  retro_atomic_store_relaxed_int(&client.frames_dropped, 0);
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg,
                                     size_t* out_index) {
  unsigned index;
  uint64_t token;
  xe_apu_credit_sink credits;
  AudioDriver* driver = nullptr;
  X_STATUS result;
  CollectRetiredClients();
  // The memory heap takes the global critical region; never hold clients_mutex_.
  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  if (!ptr) return X_STATUS_UNSUCCESSFUL;
  xe::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);
  std::unique_lock<std::mutex> clients_lock(clients_mutex_);
  if (!xe_apu_clients_reserve(client_lifetimes_.get(), &index, &token)) {
    clients_lock.unlock();
    memory()->SystemHeapFree(ptr);
    return X_STATUS_UNSUCCESSFUL;
  }
  credits.owner = client_lifetimes_.get(); credits.index = index; credits.token = token;
  try {
    result = CreateDriver(index, &credits, &driver);
  } catch (...) {
    result = X_STATUS_UNSUCCESSFUL;
  }
  if (XFAILED(result) || !driver) {
    clients_lock.unlock();
    if (driver) DestroyDriver(driver);
    memory()->SystemHeapFree(ptr);
    xe_apu_clients_abandon(client_lifetimes_.get(), index, token);
    return XFAILED(result) ? result : X_STATUS_UNSUCCESSFUL;
  }
  clients_[index].driver = driver;
  clients_[index].callback = callback;
  clients_[index].callback_arg = callback_arg;
  clients_[index].wrapped_callback_arg = ptr;
  clients_[index].next_pump_us = 0;
  retro_atomic_store_relaxed_int(&clients_[index].frames_submitted, 0);
  retro_atomic_store_relaxed_int(&clients_[index].frames_processed, 0);
  retro_atomic_store_relaxed_int(&clients_[index].frames_dropped, 0);
  if (!xe_apu_clients_publish(client_lifetimes_.get(), index, token, queued_frames_)) {
    ClearClientSlot(index);
    clients_lock.unlock();
    DestroyDriver(driver);
    memory()->SystemHeapFree(ptr);
    xe_apu_clients_abandon(client_lifetimes_.get(), index, token);
    return X_STATUS_UNSUCCESSFUL;
  }
  pending_work_event_->Set();
  if (out_index) *out_index = index;
  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, float* samples) {
  uint64_t token;
  SCOPE_profile_cpu_f("apu");
  // Still serializes SPSC producers; callback lifetime no longer uses this lock.
  std::lock_guard<std::mutex> clients_lock(clients_mutex_);
  if (index >= kMaximumClientCount || !samples ||
      !xe_apu_clients_active(client_lifetimes_.get(), (unsigned)index, &token) ||
      !clients_[index].driver) return;
  retro_atomic_fetch_add_int(&clients_[index].frames_submitted, 1);
  retro_atomic_fetch_add_int(&clients_[index].frames_processed, 1);
  clients_[index].driver->SubmitFrame(samples);
}

bool AudioSystem::GetClientPerformance(size_t index, ClientPerformance* out_perf) {
  uint64_t token, after;
  ClientPerformance result;
  if (!out_perf || index >= kMaximumClientCount ||
      !xe_apu_clients_active(client_lifetimes_.get(), (unsigned)index, &token)) return false;
  result.frames_submitted = (uint32_t)retro_atomic_load_relaxed_int(&clients_[index].frames_submitted);
  result.frames_processed = (uint32_t)retro_atomic_load_relaxed_int(&clients_[index].frames_processed);
  result.frames_dropped = (uint32_t)retro_atomic_load_relaxed_int(&clients_[index].frames_dropped);
  if (!xe_apu_clients_active(client_lifetimes_.get(), (unsigned)index, &after) || after != token)
    return false;
  *out_perf = result;
  return true;
}

bool AudioSystem::BeginClientCallback(size_t index, uint64_t expected,
                                      uint32_t* callback, uint32_t* argument,
                                      uint64_t* lease) {
  uint64_t current;
  std::lock_guard<std::mutex> clients_lock(clients_mutex_);
  if (index >= kMaximumClientCount || !callback || !argument || !lease ||
      !xe_apu_clients_active(client_lifetimes_.get(), (unsigned)index, &current) ||
      current != expected || !clients_[index].driver ||
      !xe_apu_clients_acquire(client_lifetimes_.get(), (unsigned)index, lease)) return false;
  *callback = clients_[index].callback;
  *argument = clients_[index].wrapped_callback_arg;
  return true;
}

void AudioSystem::EndClientCallback(size_t index, uint64_t lease) {
  // Callback return only publishes readiness; no guest heap work on this path.
  if (index < kMaximumClientCount)
    xe_apu_clients_release(client_lifetimes_.get(), (unsigned)index, lease);
}

void AudioSystem::CollectRetiredClients() {
  uint64_t token;
  unsigned i;
  for (i = 0; i < kMaximumClientCount; ++i)
    if (xe_apu_clients_collect(client_lifetimes_.get(), i, &token)) CleanupClient(i, token);
}

void AudioSystem::CleanupClient(size_t index, uint64_t token) {
  AudioDriver* driver;
  uint32_t argument;
  {
    std::lock_guard<std::mutex> clients_lock(clients_mutex_);
    driver = clients_[index].driver;
    argument = clients_[index].wrapped_callback_arg;
    ClearClientSlot(index);
  }
  // No callback/submitter can use these; the slot stays unavailable until finish.
  DestroyDriver(driver);
  if (argument) memory()->SystemHeapFree(argument);
  if (!xe_apu_clients_finish(client_lifetimes_.get(), (unsigned)index, token))
    XELOGE("AudioSystem: stale client cleanup for {}", index);
}

void AudioSystem::UnregisterClient(size_t index) {
  const bool self = worker_thread_ && kernel::XThread::IsInThread(worker_thread_.get());
  UnregisterClientInternal(index, self);
}

void AudioSystem::UnregisterClientInternal(size_t index, bool self) {
  uint64_t token;
  int action;
  if (index >= kMaximumClientCount) return;
  {
    std::lock_guard<std::mutex> clients_lock(clients_mutex_);
    action = xe_apu_clients_retire(client_lifetimes_.get(), (unsigned)index, &token);
    if (action == XE_APU_RETIRE_NONE) return;
    clients_[index].next_pump_us = 0;
  }
  if (action == XE_APU_RETIRE_OWN) CleanupClient(index, token);
  else if (!self) {
    xe_apu_clients_wait_callback(client_lifetimes_.get(), (unsigned)index, token, -1);
    if (xe_apu_clients_claim_cleanup(client_lifetimes_.get(), (unsigned)index, token))
      CleanupClient(index, token);
  }
  // A borrowed self-unregister keeps resources until callback return and a cold
  // register/unregister/shutdown collector. The slot is unavailable meanwhile.
}

void AudioSystem::Pause() {
  if (!PauseForState(2000000)) XELOGE("AudioSystem: worker pause timed out");
}

bool AudioSystem::PauseForState(int64_t timeout_us) {
  if(worker_thread_ && kernel::XThread::IsInThread(worker_thread_.get())) return false;
  return xe_state_control_acquire(&pause_control_,timeout_us)!=0;
}

void AudioSystem::Resume() {
  xe_state_control_release(&pause_control_);
}

}  // namespace apu
}  // namespace xe
