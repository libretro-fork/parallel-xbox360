/*
 * Xenia Edge - Xbox 360 Emulator Libretro Core
 * Libretro Graphics System Implementation
 * Copyright (C) 2024 Xenia Edge Contributors
 */

#include "libretro_graphics_system.h"


namespace xe {
namespace gpu {
namespace libretro_gpu {

// ---- LibretroCommandProcessor ---------------------------------------------

LibretroCommandProcessor::LibretroCommandProcessor(
    GraphicsSystem* graphics_system, kernel::KernelState* kernel_state,
    FrameSyncState* sync)
    : CommandProcessor(graphics_system, kernel_state), sync_(sync) {}

LibretroCommandProcessor::~LibretroCommandProcessor() = default;

void LibretroCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {}

bool LibretroCommandProcessor::SetupContext() {
  return CommandProcessor::SetupContext();
}

void LibretroCommandProcessor::ShutdownContext() {
  CommandProcessor::ShutdownContext();
}

void LibretroCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                          uint32_t frontbuffer_width,
                                          uint32_t frontbuffer_height) {
  if (!sync_) return;

  // Store frame dimensions
  retro_atomic_store_relaxed_int(&sync_->frame_width, frontbuffer_width ? frontbuffer_width : 1280);
  retro_atomic_store_relaxed_int(&sync_->frame_height, frontbuffer_height ? frontbuffer_height : 720);
  retro_atomic_fetch_add_size(&sync_->frame_count, 1);

  RecordGuestSwap();
}

Shader* LibretroCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                              const uint32_t* host_address,
                                              uint32_t dword_count) {
  return nullptr;
}

bool LibretroCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                          uint32_t index_count,
                                          IndexBufferInfo* index_buffer_info,
                                          bool major_mode_explicit) {
  return true;
}

bool LibretroCommandProcessor::IssueCopy() { return true; }

// ---- LibretroGraphicsSystem -----------------------------------------------

LibretroGraphicsSystem::LibretroGraphicsSystem() {}

LibretroGraphicsSystem::~LibretroGraphicsSystem() = default;

X_STATUS LibretroGraphicsSystem::Setup(cpu::Processor* processor,
                                        kernel::KernelState* kernel_state,
                                        bool with_presentation) {
  // Skip Vulkan provider - frontend owns GPU context.
  return GraphicsSystem::Setup(processor, kernel_state, false);
}

void LibretroGraphicsSystem::Shutdown() {
  GraphicsSystem::Shutdown();
}

std::unique_ptr<CommandProcessor>
LibretroGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new LibretroCommandProcessor(this, kernel_state_, &sync_state_));
}

}  // namespace libretro_gpu
}  // namespace gpu
}  // namespace xe
