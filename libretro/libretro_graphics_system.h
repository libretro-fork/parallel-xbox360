/*
 * Xenia Edge - Xbox 360 Emulator Libretro Core
 * Libretro Graphics System
 * Copyright (C) 2024 Xenia Edge Contributors
 *
 * Extends NullGraphicsSystem with published frame dimensions and counters.
 * The frontend owns presentation cadence.
 */

#ifndef LIBRETRO_GRAPHICS_SYSTEM_H
#define LIBRETRO_GRAPHICS_SYSTEM_H

#include <atomic>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/null/null_graphics_system.h"

namespace xe {
namespace gpu {
namespace libretro_gpu {

// Shared state between the GPU command processor thread and the
// libretro frontend thread (retro_run).
struct FrameSyncState {
  std::atomic<uint32_t> frame_width{1280};
  std::atomic<uint32_t> frame_height{720};
  std::atomic<uint64_t> frame_count{0};
};

class LibretroCommandProcessor : public CommandProcessor {
 public:
  LibretroCommandProcessor(GraphicsSystem* graphics_system,
                           kernel::KernelState* kernel_state,
                           FrameSyncState* sync);
  ~LibretroCommandProcessor() override;

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;
  void RestoreEdramSnapshot(const void* snapshot) override;

 private:
  bool SetupContext() override;
  void ShutdownContext() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;

  void InitializeTrace() override;

  FrameSyncState* sync_ = nullptr;
};

class LibretroGraphicsSystem : public null::NullGraphicsSystem {
 public:
  LibretroGraphicsSystem();
  ~LibretroGraphicsSystem() override;

  std::string name() const override { return "libretro"; }

  X_STATUS Setup(cpu::Processor* processor,
                 kernel::KernelState* kernel_state,
                 bool with_presentation) override;

  void Shutdown() override;

  FrameSyncState* sync_state() { return &sync_state_; }

 protected:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

 private:
  FrameSyncState sync_state_;
};

}  // namespace libretro_gpu
}  // namespace gpu
}  // namespace xe

#endif  // LIBRETRO_GRAPHICS_SYSTEM_H
