/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_system.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_stream.h"
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

  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    client_semaphores_[i] = xe::threading::Semaphore::Create(0, queued_frames_);
  }
  pending_work_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(pending_work_event_);

  xma_decoder_ = std::make_unique<xe::apu::XmaDecoder>(processor_);


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

  size_t next_client = 0;
  // Output queue space and driver credits drive callbacks. No host clock,
  // deadline or timed sleep may limit production during fast-forward.
  while (retro_atomic_load_acquire_int(&worker_running_)) {
    xe::threading::WaitHandle* handles[kMaximumClientCount + 1];
    size_t indices[kMaximumClientCount + 1];
    size_t count = 0, client_index, i;
    if (is_paused()) {
      pause_fence_.Signal();
      xe_pause_gate_wait(pause_gate_.get());
      continue;
    }
    {
      std::lock_guard<std::mutex> clients_lock(clients_mutex_);
      for (i = 0; i < kMaximumClientCount; ++i) {
        client_index = (next_client + i) % kMaximumClientCount;
        if (!clients_[client_index].in_use || !clients_[client_index].driver ||
            !clients_[client_index].driver->CanSubmitFrame()) continue;
        handles[count] = client_semaphores_[client_index].get();
        indices[count++] = client_index;
      }
    }
    // The control event covers registration, output consumption, pause and
    // shutdown. Its stored signal closes the scan-to-wait wakeup window.
    handles[count++] = pending_work_event_.get();
    auto result = xe::threading::WaitAny(handles, count, true);
    if (result.first != xe::threading::WaitResult::kSuccess ||
        result.second + 1 == count || !retro_atomic_load_acquire_int(&worker_running_)) continue;
    client_index = indices[result.second];
    if (is_paused()) {
      client_semaphores_[client_index]->Release(1, nullptr);
      continue;
    }
    next_client = (client_index + 1) % kMaximumClientCount;

    // UnregisterClient waits on this after clearing in_use.
    std::lock_guard<std::mutex> cb_lk(clients_[client_index].callback_mutex);

    uint32_t client_callback = 0;
    uint32_t client_callback_arg = 0;
    {
      std::lock_guard<std::mutex> clients_lock(clients_mutex_);
      if (clients_[client_index].in_use) {
        client_callback = clients_[client_index].callback;
        client_callback_arg = clients_[client_index].wrapped_callback_arg;
      }
    }

    if (client_callback) {
      SCOPE_profile_cpu_i("apu", "xe::apu::AudioSystem->client_callback");
      uint64_t args[] = {client_callback_arg};
      processor_->Execute(worker_thread_->thread_state(), client_callback, args,
                          xe::countof(args));
    }
  }
  retro_atomic_store_release_int(&worker_running_, 0);

  // TODO(benvanik): call module API to kill?
}

int AudioSystem::FindFreeClient() {
  for (int i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      return i;
    }
  }

  return -1;
}

void AudioSystem::Initialize() {}

void AudioSystem::StopWorker() {
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
  StopWorker();

  // Unregister all active clients to shut down their audio drivers before
  // the semaphores are destroyed with this AudioSystem.
  uint32_t wrapped_callback_args[kMaximumClientCount] = {};
  {
    std::lock_guard<std::mutex> clients_lock(clients_mutex_);
    for (size_t i = 0; i < kMaximumClientCount; ++i) {
      if (clients_[i].in_use) {
        DestroyDriver(clients_[i].driver);
        wrapped_callback_args[i] = clients_[i].wrapped_callback_arg;
        clients_[i].driver = nullptr;
        clients_[i].callback = 0;
        clients_[i].callback_arg = 0;
        clients_[i].wrapped_callback_arg = 0;
        clients_[i].in_use = false;
      }
    }
  }
  // Outside clients_mutex_, as the heap takes the global lock.
  for (uint32_t wrapped_callback_arg : wrapped_callback_args) {
    if (wrapped_callback_arg) {
      memory()->SystemHeapFree(wrapped_callback_arg);
    }
  }
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg,
                                     size_t* out_index) {
  // Outside clients_mutex_, as the heap takes the global lock.
  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  xe::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);

  std::unique_lock<std::mutex> clients_lock(clients_mutex_);

  auto index = FindFreeClient();
  assert_true(index >= 0);
  if (index < 0) {
    clients_lock.unlock();
    memory()->SystemHeapFree(ptr);
    return X_STATUS_UNSUCCESSFUL;
  }

  auto client_semaphore = client_semaphores_[index].get();
  auto ret = client_semaphore->Release(queued_frames_, nullptr);
  assert_true(ret);

  AudioDriver* driver;
  auto result = CreateDriver(index, client_semaphore, &driver);
  if (XFAILED(result)) {
    XELOGE("AudioSystem::RegisterClient: CreateDriver failed for index={}",
           index);
    clients_lock.unlock();
    memory()->SystemHeapFree(ptr);
    return result;
  }
  assert_not_null(driver);
  XELOGI(
      "AudioSystem::RegisterClient: driver created for index={}, driver={:p}",
      index, (void*)driver);

  clients_[index].driver = driver;
  clients_[index].callback = callback;
  clients_[index].callback_arg = callback_arg;
  clients_[index].wrapped_callback_arg = ptr;
  clients_[index].in_use = true;
  retro_atomic_store_relaxed_int(&clients_[index].frames_submitted, 0);
  retro_atomic_store_relaxed_int(&clients_[index].frames_processed, 0);
  retro_atomic_store_relaxed_int(&clients_[index].frames_dropped, 0);

  // Wake the worker to include this client in the output readiness scan.
  pending_work_event_->Set();

  XELOGI("AudioSystem::RegisterClient: client {} registered successfully",
         index);

  if (out_index) {
    *out_index = index;
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, float* samples) {
  SCOPE_profile_cpu_f("apu");

  std::lock_guard<std::mutex> clients_lock(clients_mutex_);
  assert_true(index < kMaximumClientCount);
  if (index >= kMaximumClientCount || !clients_[index].in_use ||
      !clients_[index].driver) {
    XELOGW(
        "SubmitFrame called for invalid/unregistered client index {} "
        "(in_use={}, driver={:p})",
        index, index < kMaximumClientCount ? clients_[index].in_use : false,
        index < kMaximumClientCount ? (void*)clients_[index].driver : nullptr);

    // Submit silence instead of dropping the frame to maintain the callback
    // chain.  If we don't submit anything, the audio driver's OnBufferEnd
    // callback will never fire, causing the semaphore to leak.
    if (index < kMaximumClientCount && clients_[index].driver) {
      static float silence[apu::AudioDriver::kFrameSamplesMax] = {0};
      retro_atomic_fetch_add_int(&clients_[index].frames_dropped, 1);
      (clients_[index].driver)->SubmitFrame(silence);
    } else if (index < kMaximumClientCount) {
      // Tick the semaphore so the worker doesn't stall on a dead client.
      client_semaphores_[index]->Release(1, nullptr);
    }
    return;
  }
  retro_atomic_fetch_add_int(&clients_[index].frames_submitted, 1);
  retro_atomic_fetch_add_int(&clients_[index].frames_processed, 1);
  (clients_[index].driver)->SubmitFrame(samples);
}

bool AudioSystem::GetClientPerformance(size_t index,
                                       ClientPerformance* out_perf) {
  if (index >= kMaximumClientCount || !out_perf) {
    return false;
  }

  if (!clients_[index].in_use) {
    return false;
  }

  out_perf->frames_submitted = static_cast<uint32_t>(retro_atomic_load_relaxed_int(&clients_[index].frames_submitted));
  out_perf->frames_processed = static_cast<uint32_t>(retro_atomic_load_relaxed_int(&clients_[index].frames_processed));
  out_perf->frames_dropped = static_cast<uint32_t>(retro_atomic_load_relaxed_int(&clients_[index].frames_dropped));
  return true;
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  assert_true(index < kMaximumClientCount);
  AudioDriver* driver_to_destroy;
  {
    std::lock_guard<std::mutex> clients_lock(clients_mutex_);
    XELOGI(
        "AudioSystem::UnregisterClient: index={}, driver={:p}", index,
        index < kMaximumClientCount ? (void*)clients_[index].driver : nullptr);
    driver_to_destroy = clients_[index].driver;
    // Leak wrapped_callback_arg: in-flight callback may hold this pointer.
    clients_[index].driver = nullptr;
    clients_[index].callback = 0;
    clients_[index].callback_arg = 0;
    clients_[index].wrapped_callback_arg = 0;
    clients_[index].in_use = false;
    retro_atomic_store_relaxed_int(&clients_[index].frames_submitted, 0);
    retro_atomic_store_relaxed_int(&clients_[index].frames_processed, 0);
    retro_atomic_store_relaxed_int(&clients_[index].frames_dropped, 0);
  }

  // Wait for any in-flight callback; can't hold clients_mutex_ (callback
  // re-enters).
  {
    std::lock_guard<std::mutex> lk(clients_[index].callback_mutex);
  }

  DestroyDriver(driver_to_destroy);

  // Drain the semaphore of its count.
  auto client_semaphore = client_semaphores_[index].get();
  xe::threading::WaitResult wait_result;
  do {
    wait_result = xe::threading::Wait(client_semaphore, false,
                                      std::chrono::milliseconds(0));
  } while (wait_result == xe::threading::WaitResult::kSuccess);
  assert_true(wait_result == xe::threading::WaitResult::kTimeout);
}

bool AudioSystem::Save(ByteStream* stream) {
  stream->Write(kAudioSaveSignature);

  // Count the number of used clients first.
  // Any gaps should be handled gracefully.
  uint32_t used_clients = 0;
  for (int i = 0; i < kMaximumClientCount; i++) {
    if (clients_[i].in_use) {
      used_clients++;
    }
  }

  stream->Write(used_clients);
  for (uint32_t i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      continue;
    }

    stream->Write(i);
    stream->Write(client.callback);
    stream->Write(client.callback_arg);
    stream->Write(client.wrapped_callback_arg);
  }

  return true;
}

bool AudioSystem::Restore(ByteStream* stream) {
  if (stream->Read<uint32_t>() != kAudioSaveSignature) {
    XELOGE("AudioSystem::Restore - Invalid magic value!");
    return false;
  }

  uint32_t num_clients = stream->Read<uint32_t>();
  for (uint32_t i = 0; i < num_clients; i++) {
    auto id = stream->Read<uint32_t>();
    assert_true(id < kMaximumClientCount);

    auto& client = clients_[id];

    // Reset the semaphore and recreate the driver ourselves.
    if (client.driver) {
      UnregisterClient(id);
    }

    client.callback = stream->Read<uint32_t>();
    client.callback_arg = stream->Read<uint32_t>();
    client.wrapped_callback_arg = stream->Read<uint32_t>();

    client.in_use = true;

    auto client_semaphore = client_semaphores_[id].get();
    auto ret = client_semaphore->Release(queued_frames_, nullptr);
    assert_true(ret);

    AudioDriver* driver = nullptr;
    auto status = CreateDriver(id, client_semaphore, &driver);
    if (XFAILED(status)) {
      XELOGE(
          "AudioSystem::Restore - Call to CreateDriver failed with status "
          "{:08X}",
          status);
      return false;
    }

    assert_not_null(driver);
    client.driver = driver;
  }

  return true;
}

void AudioSystem::Pause() {
  if (is_paused()) {
    return;
  }
  xe_pause_gate_set(pause_gate_.get(), 1);

  // Kind of a hack, but it works.
  pending_work_event_->Set();
  pause_fence_.Wait();

  xma_decoder_->Pause();
}

void AudioSystem::Resume() {
  if (!is_paused()) {
    return;
  }
  xe_pause_gate_set(pause_gate_.get(), 0);



  xma_decoder_->Resume();
}

}  // namespace apu
}  // namespace xe
