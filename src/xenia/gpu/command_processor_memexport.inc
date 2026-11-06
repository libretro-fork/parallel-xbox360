/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Two-buffer memexport page tracking, shared by the D3D12 and Vulkan command
// processors. This is #included into each backend's class body (like the pm4
// command processor sections) so each backend gets its own inlinable copy with
// no inheritance or virtual dispatch. The include site must already provide
// SharedMemory, Shader, RegisterFile, xenos, xe::bit_scan_forward and the
// C89 core_memexport.h declarations. All bitmap mutation/plan use is on the
// GPU command thread; reset occurs after that thread has stopped.
// No include guard: this is injected into a class body, not a standalone
// header.
//
// It must also define, before including this:
//   void FlushMemexportStagingReadback()
//     Copies export output the backend staged for readback into guest RAM, now
//     that the wait has made it available. Empty where output already lands
//     there, which is zero-copy and the host buffer.
//   void AwaitMemexportSubmission(uint64_t submission)
//     Waits for a submission to complete, ending it first if it is the one
//     still being recorded.

// Records the guest pages any memexport draw wrote so geometry draws consuming
// that output route to the host-imported buffer. Marking rounds outward. Only
// awaited output (IsMemexportAwaited) holds back fences and coherency requests.
void MarkMemexportPagesWritten(uint32_t base_bytes, uint32_t size_bytes,
                               bool awaited) {
  if (!xe_memexport_mark(&memexport_pages_, base_bytes, size_bytes)) return;
  memexport_last_submission_ = GetCurrentSubmission();
  if (awaited) {
    memexport_await_pending_ = true;
    memexport_awaited_submission_ = memexport_last_submission_;
  }
}

// Waits for export output recorded since the last wait to reach guest RAM, so a
// fence or interrupt the guest is about to observe means the data is there.
// Ring buffer read-pointer writeback is deliberately not covered: it reports
// parse progress rather than execution, but fires per batch, so stalling there
// would serialize the command processor against the GPU.
void AwaitMemexportForFence() {
  if (!memexport_await_pending_) {
    return;
  }
  memexport_await_pending_ = false;
  // Only as far as the last awaited export, which is submitted first if it is
  // still only recorded.
  AwaitMemexportSubmission(memexport_awaited_submission_);
  FlushMemexportStagingReadback();
}

// Same, for the guest asking the GPU to flush a range it is about to read
// (COHER_BASE_HOST/COHER_SIZE_HOST). Gated on the range actually holding export
// output, unlike the fence path which cannot know what the fence refers to.
void AwaitMemexportForCoherency(uint32_t base_bytes, uint32_t size_bytes) {
  if (!memexport_await_pending_ || !IsMemexportRange(base_bytes, size_bytes)) {
    return;
  }
  memexport_await_pending_ = false;
  AwaitMemexportSubmission(memexport_awaited_submission_);
  FlushMemexportStagingReadback();
}

// Clears memexport marks for a range a resolve just wrote, so its device-buffer
// output is no longer treated as memexport (host buffer) data. Clearing rounds
// inward: a page straddling the edge holds both this range's data and
// neighbouring memexport output, so it keeps its mark. The window is left
// as-is, it only needs to bound the set bits.
void ClearMemexportPages(uint32_t base_bytes, uint32_t size_bytes) {
  xe_memexport_clear(&memexport_pages_, base_bytes, size_bytes);
}

bool IsMemexportRange(uint32_t base_bytes, uint32_t size_bytes) const {
  return xe_memexport_has(&memexport_pages_, base_bytes, size_bytes) != 0;
}

bool VertexFetchInMemexportRange(const RegisterFile& regs,
                                 const Shader& vertex_shader) const {
  const Shader::ConstantRegisterMap& constant_map =
      vertex_shader.constant_register_map();
  for (uint32_t i = 0; i < xe::countof(constant_map.vertex_fetch_bitmap); ++i) {
    uint32_t vfetch_bits = constant_map.vertex_fetch_bitmap[i];
    uint32_t j;
    while (xe::bit_scan_forward(vfetch_bits, &j)) {
      vfetch_bits = xe::clear_lowest_bit(vfetch_bits);
      xenos::xe_gpu_vertex_fetch_t vfetch_constant =
          regs.GetVertexFetch(i * 32 + j);
      if (vfetch_constant.type != xenos::FetchConstantType::kVertex) {
        continue;
      }
      if (IsMemexportRange(vfetch_constant.address << 2,
                           vfetch_constant.size << 2)) {
        return true;
      }
    }
  }
  return false;
}

// Resets all tracking, called from each backend's ShutdownContext.
void ResetMemexportPages() {
  xe_memexport_reset(&memexport_pages_);
  memexport_await_pending_ = false;
  memexport_last_submission_ = 0;
  memexport_awaited_submission_ = 0;
}

// Guest 4 KiB pages are owned by one GPU command thread. Plans borrow these
// words only while recording synchronously, with no intervening bitmap writes.
static_assert(XE_MEMEXPORT_BUFFER_SIZE == SharedMemory::kBufferSize);
xe_memexport_map memexport_pages_{};
// Export output has been recorded but not yet awaited for the guest to read.
bool memexport_await_pending_ = false;
// Submission the last export draw was recorded in.
uint64_t memexport_last_submission_ = 0;
// Same, of the last export draw that is awaited.
uint64_t memexport_awaited_submission_ = 0;
