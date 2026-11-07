/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/graphics_system.h"

#include <retro_atomic.h>

#include "libretro/core_state_budget.h"
#include <features/features_cpu.h>

#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/filesystem.h"
#include <cstring>
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/config.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/graphics_provider.h"

DEFINE_uint32(internal_display_resolution, 8,
              "Allow games that support different resolutions to render "
              "in a specific resolution.\n"
              "This is not guaranteed to work with all games or improve "
              "performance.\n"
              "   0=640x480\n"
              "   1=640x576\n"
              "   2=720x480\n"
              "   3=720x576\n"
              "   4=800x600\n"
              "   5=848x480\n"
              "   6=1024x768\n"
              "   7=1152x864\n"
              "   8=1280x720 (Default)\n"
              "   9=1280x768\n"
              "   10=1280x960\n"
              "   11=1280x1024\n"
              "   12=1360x768\n"
              "   13=1440x900\n"
              "   14=1680x1050\n"
              "   15=1920x540\n"
              "   16=1920x1080\n"
              "   17=internal_display_resolution_x/y",
              "Console");
DEFINE_uint32(internal_display_resolution_x, 1280,
              "Custom width. See internal_display_resolution. Range 1-1920.",
              "Video");
DEFINE_uint32(internal_display_resolution_y, 720,
              "Custom height. See internal_display_resolution. Range 1-1080.\n",
              "Video");

DEFINE_bool(
    store_shaders, true,
    "Store shaders persistently and load them when loading games to avoid "
    "runtime spikes and freezes when playing the game not for the first time.",
    "GPU.Debug");

namespace xe {
namespace gpu {

// Nvidia Optimus/AMD PowerXpress support.
// These exports force the process to trigger the discrete GPU in multi-GPU
// systems.
// https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// https://stackoverflow.com/questions/17458803/amd-equivalent-to-nvoptimusenablement
#if XE_PLATFORM_WIN32
extern "C" {
__declspec(dllexport) uint32_t NvOptimusEnablement = 0x00000001;
__declspec(dllexport) uint32_t AmdPowerXpressRequestHighPerformance = 1;
}  // extern "C"
#endif  // XE_PLATFORM_WIN32

GraphicsSystem::GraphicsSystem() {
  register_file_ = reinterpret_cast<RegisterFile*>(memory::AllocFixed(
      nullptr, sizeof(RegisterFile), memory::AllocationType::kReserveCommit,
      memory::PageAccess::kReadWrite));
}

GraphicsSystem::~GraphicsSystem() {
  xe_vblank_queue_free(vblank_queue_);
}

X_STATUS GraphicsSystem::Setup(cpu::Processor* processor,
                               kernel::KernelState* kernel_state,
                               bool with_presentation) {
  memory_ = processor->memory();
  processor_ = processor;
  kernel_state_ = kernel_state;

  scaled_aspect_x_ = 16;
  scaled_aspect_y_ = 9;

  auto custom_res_x = cvars::internal_display_resolution_x;
  auto custom_res_y = cvars::internal_display_resolution_y;
  if (!custom_res_x || custom_res_x > 1920 || !custom_res_y ||
      custom_res_y > 1080) {
    OVERRIDE_PERSIST_uint32(internal_display_resolution_x,
                            internal_display_resolution_entries[8].first);
    OVERRIDE_PERSIST_uint32(internal_display_resolution_y,
                            internal_display_resolution_entries[8].second);
    config::SaveConfig();
    xe::FatalError(fmt::format(
        "Invalid custom resolution specified: {}x{}\n"
        "Width must be between 1-1920.\nHeight must be between 1-1080.",
        custom_res_x, custom_res_y));
  }

  if (with_presentation && provider_) {
    presenter_ = provider_->CreatePresenter();
  }

  // Create command processor. This will spin up a thread to process all
  // incoming ringbuffer packets.
  command_processor_ = CreateCommandProcessor();
  if (!command_processor_->Initialize()) {
    XELOGE("Unable to initialize command processor");
    return X_STATUS_UNSUCCESSFUL;
  }

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FC80000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(ReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(WriteRegisterThunk));

  // Guest interrupts require an XHostThread, but only frontend requests wake it.
  vblank_queue_ = xe_vblank_queue_new();
  if (!vblank_queue_) return X_STATUS_UNSUCCESSFUL;
  vblank_worker_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          kernel_state_, 128 * 1024, 0,
          [this]() {
            while (xe_vblank_queue_take(vblank_queue_)) {
              if (kernel_state_->is_title_open()) MarkVblank();
            }
            return 0;
          },
          kernel_state->GetIdleProcess()));
  vblank_worker_thread_->set_can_debugger_suspend(true);
  vblank_worker_thread_->set_name("Guest vblank");
  vblank_worker_thread_->Create();
  vblank_worker_thread_->thread()->set_priority(threading::ThreadPriority::kLowest);
  if (cvars::trace_gpu_stream) {
    BeginTracing();
  }

  return X_STATUS_SUCCESS;
}

void GraphicsSystem::Shutdown() {
  if (command_processor_) {
    EndTracing();
    command_processor_->Shutdown();
    command_processor_.reset();
  }

  StopVblankWorker();

  presenter_.reset();
  provider_.reset();
}

void GraphicsSystem::LibretroVblank() {
  xe_vblank_queue_request(vblank_queue_);
}

void GraphicsSystem::StopVblankWorker() {
  xe_vblank_queue_stop(vblank_queue_);
  if (vblank_worker_thread_) {
    vblank_worker_thread_->Wait(0, 0, 0, nullptr);
    vblank_worker_thread_.reset();
  }
}
void GraphicsSystem::OnHostGpuLossFromAnyThread(
    [[maybe_unused]] bool is_responsible) {
  if (retro_atomic_exchange_int(&host_gpu_loss_reported_,1)) {
    return;
  }

  config::SaveConfig();

  xe::FatalError("Graphics device lost (probably due to an internal error)");
}

uint32_t GraphicsSystem::ReadRegisterThunk(void* ppc_context,
                                           GraphicsSystem* gs, uint32_t addr) {
  return gs->ReadRegister(addr);
}

void GraphicsSystem::WriteRegisterThunk(void* ppc_context, GraphicsSystem* gs,
                                        uint32_t addr, uint32_t value) {
  gs->WriteRegister(addr, value);
}

uint32_t GraphicsSystem::ReadRegister(uint32_t addr) {
  uint32_t r = (addr & 0xFFFF) / 4;

  switch (r) {
    case XE_GPU_REG_RB_EDRAM_TIMING:
      return 0x08100748;
    case XE_GPU_REG_RB_BC_CONTROL:
      return 0x0000200E;
    case XE_GPU_REG_D1MODE_V_COUNTER: {
      // Free-running scanline counter, like Xenos drives off the pixel clock.
      // Cycles 0..(total_lines-1) every frame, including the vertical-blank
      // region above the 720 active lines reported by D1MODE_VIEWPORT_SIZE
      // below. Total ~= active + 4% blanking (720p60 -> 750, 576p50 -> 625).
      // Period is measured from the MarkVblank cadence, so V_COUNTER stays
      // coupled to whatever vblank rate is actually in effect (50Hz, 60Hz,
      // or the frontend target refresh rate) — matching how silicon raises vblank IRQs from
      // V_COUNTER crossings.
      const uint32_t vblank_hz = GetGuestVblankRateHz();
      const uint32_t total_lines = (vblank_hz == 50) ? 625u : 750u;
      uint64_t period = uint64_t(retro_atomic_load_acquire_64(&vblank_period_ticks_));
      if (!period) {
        // Pre-first-vblank bootstrap: assume the configured rate.
        period = Clock::guest_tick_frequency() / vblank_hz;
        if (!period) {
          return 0;
        }
      }
      const uint64_t last =
          uint64_t(retro_atomic_load_acquire_64(&last_vblank_guest_tick_));
      if (!last) {
        return 0;
      }
      const uint64_t now = Clock::QueryGuestTickCount();
      uint64_t delta = (now > last) ? (now - last) : 0;
      if (delta >= period) {
        // Reader outran the next vblank (stall, paused, etc.); park on the
        // last line until MarkVblank advances the anchor.
        delta = period - 1;
      }
      return static_cast<uint32_t>((delta * total_lines) / period);
    }
    case XE_GPU_REG_D1MODE_VBLANK_VLINE_STATUS:
      return 1;  // vblank
    case XE_GPU_REG_D1MODE_VIEWPORT_SIZE:
      // 1280x720, [width(0x0FFF), height(0x0FFF)].
      return 0x050002D0;
    default:
      if (!register_file()->IsValidRegister(r)) {
        XELOGE("GPU: Read from unknown register ({:04X})", r);
      }
  }

  assert_true(r < RegisterFile::kRegisterCount);
  return register_file()->values[r];
}

void GraphicsSystem::WriteRegister(uint32_t addr, uint32_t value) {
  uint32_t r = (addr & 0xFFFF) / 4;

  switch (r) {
    case 0x01C5:  // CP_RB_WPTR
      command_processor_->UpdateWritePointer(value);
      break;
    case 0x1844:  // AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS
      break;
    default:
      XELOGW("Unknown GPU register {:04X} write: {:08X}", r, value);
      break;
  }

  assert_true(r < RegisterFile::kRegisterCount);
  this->register_file()->values[r] = value;
}

void GraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {
  command_processor_->InitializeRingBuffer(ptr, size_log2);
}

void GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                uint32_t block_size_log2) {
  command_processor_->EnableReadPointerWriteBack(ptr, block_size_log2);
}

void GraphicsSystem::SetInterruptCallback(uint32_t callback,
                                          uint32_t user_data) {
  interrupt_callback_ = callback;
  interrupt_callback_data_ = user_data;
  XELOGGPU("SetInterruptCallback({:08X}, {:08X})", callback, user_data);
}

void GraphicsSystem::DispatchInterruptCallback(uint32_t source, uint32_t cpu) {
  kernel_state()->EmulateCPInterruptDPC(interrupt_callback_,
                                        interrupt_callback_data_, source, cpu);
}

extern retro_atomic_size_t g_guest_vblanks;

void GraphicsSystem::MarkVblank() {
  SCOPE_profile_cpu_f("gpu");
  retro_atomic_fetch_add_size(&g_guest_vblanks, 1);

  // Capture vblank cadence so D1MODE_V_COUNTER tracks the actual rate
  // (50Hz, 60Hz, or the frontend target refresh rate), not just the configured one.
  const uint64_t now = Clock::QueryGuestTickCount();
  const uint64_t prev =
      uint64_t(retro_atomic_exchange_64(&last_vblank_guest_tick_,int64_t(now)));
  if (prev && now > prev) {
    retro_atomic_store_release_64(&vblank_period_ticks_,int64_t(now-prev));
  }

  // Increment vblank counter (so the game sees us making progress).
  command_processor_->increment_counter();

  // TODO(benvanik): we shouldn't need to do the dispatch here, but there's
  //     something wrong and the CP will block waiting for code that
  //     needs to be run in the interrupt.
  DispatchInterruptCallback(0, 2);
}

void GraphicsSystem::ClearCaches() {
  command_processor_->CallInThread([](void* context,const void*,size_t) {
    static_cast<CommandProcessor*>(context)->ClearCaches();
  },command_processor_.get());
}

void GraphicsSystem::InvalidateGpuMemory() {
  command_processor_->CallInThread([](void* context,const void*,size_t) {
    static_cast<CommandProcessor*>(context)->InvalidateGpuMemory();
  },command_processor_.get());
}

void GraphicsSystem::InitializeShaderStorage(
    const std::filesystem::path& cache_root,uint32_t title_id,bool blocking) {
  if (!cvars::store_shaders) return;
  if (blocking && command_processor_->is_paused()) {
    command_processor_->InitializeShaderStorage(cache_root,title_id,true);
    return;
  }
  // Only a load-time path string is copied; no captured C++ callable or path
  // object is retained in the queue. Completion callbacks have no core caller.
  struct StorageArgs { void* fence; uint32_t title; int blocking; } args = {};
  args.title=title_id;args.blocking=blocking;
  const std::string path=xe::path_to_utf8(cache_root);
  if (path.size()>=4096) throw std::bad_alloc();
  const xe_gpu_task_fn run=[](void* context,const void* data,size_t size) {
        StorageArgs args;
        std::memcpy(&args,data,sizeof(args));
        const char* path=static_cast<const char*>(data)+sizeof(args);
        try {
          static_cast<CommandProcessor*>(context)->InitializeShaderStorage(
              xe::to_path(std::string_view(path,size-sizeof(args)-1)),
              args.title,args.blocking!=0);
        } catch (...) {
          if(args.fence) static_cast<xe::threading::Fence*>(args.fence)->Signal();
          throw;
        }
        if(args.fence) static_cast<xe::threading::Fence*>(args.fence)->Signal();
      };
  const xe_gpu_task_fn cancel=[](void*,const void* data,size_t) noexcept {
        StorageArgs args;
        std::memcpy(&args,data,sizeof(args));
        if(args.fence) static_cast<xe::threading::Fence*>(args.fence)->Signal();
      };
  if(blocking) {
    xe::threading::Fence fence;
    args.fence=&fence;
    command_processor_->CallInThreadParts(run,command_processor_.get(),
        &args,sizeof(args),path.c_str(),path.size()+1,cancel);
    fence.Wait();
  } else {
    command_processor_->CallInThreadParts(run,command_processor_.get(),
        &args,sizeof(args),path.c_str(),path.size()+1,cancel);
  }
}

void GraphicsSystem::RequestFrameTrace() {
  command_processor_->RequestFrameTrace(cvars::trace_gpu_prefix);
}

void GraphicsSystem::BeginTracing() {
  command_processor_->BeginTracing(cvars::trace_gpu_prefix);
}

void GraphicsSystem::EndTracing() { command_processor_->EndTracing(); }

void GraphicsSystem::RequestEndTracing() {
  command_processor_->RequestEndTracing();
}

bool GraphicsSystem::is_tracing_stream() const {
  return command_processor_->is_tracing_stream();
}

void GraphicsSystem::Pause() {
  if(!PauseForState(2000000)) XELOGE("Graphics workers did not reach their state pause boundary");
}

bool GraphicsSystem::PauseForState(int64_t timeout_us) {
  xe_state_budget budget;
  if(!xe_state_budget_begin(&budget,cpu_features_get_time_usec(),timeout_us) ||
      !command_processor_ || !vblank_queue_ ||
      (vblank_worker_thread_ && kernel::XThread::IsInThread(vblank_worker_thread_.get()))) return false;
  const bool had_vblank=xe_vblank_queue_is_paused(vblank_queue_)!=0;
  const bool had_gpu=command_processor_->is_paused();
  if(!xe_vblank_queue_pause(vblank_queue_,xe_state_budget_left(&budget,cpu_features_get_time_usec()))) return false;
  try {
    if(!command_processor_->PauseForState(xe_state_budget_left(&budget,cpu_features_get_time_usec()))) {
      if(!had_vblank) xe_vblank_queue_resume(vblank_queue_);
      return false;
    }
  } catch(...) {
    if(!had_vblank) xe_vblank_queue_resume(vblank_queue_);
    throw;
  }
  if(!paused_) state_pause_owned_=0;
  if(!had_vblank) state_pause_owned_|=1;
  if(!had_gpu) state_pause_owned_|=2;
  paused_=true;return true;
}

void GraphicsSystem::Resume() {
  unsigned owned=state_pause_owned_;
  state_pause_owned_=0;paused_=false;
  if((owned&2) && command_processor_) command_processor_->Resume();
  if(owned&1) xe_vblank_queue_resume(vblank_queue_);
}

bool GraphicsSystem::Save(ByteStream* stream) {
  stream->Write<uint32_t>(interrupt_callback_);
  stream->Write<uint32_t>(interrupt_callback_data_);

  return command_processor_->Save(stream);
}

bool GraphicsSystem::Restore(ByteStream* stream) {
  interrupt_callback_ = stream->Read<uint32_t>();
  interrupt_callback_data_ = stream->Read<uint32_t>();

  return command_processor_->Restore(stream);
}

std::pair<uint16_t, uint16_t> GraphicsSystem::GetInternalDisplayResolution() {
  if (cvars::internal_display_resolution >=
      internal_display_resolution_entries.size()) {
    return {cvars::internal_display_resolution_x,
            cvars::internal_display_resolution_y};
  }
  return internal_display_resolution_entries
      [cvars::internal_display_resolution];
}

}  // namespace gpu
}  // namespace xe
