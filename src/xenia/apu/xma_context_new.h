/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_CONTEXT_NEW_H_
#define XENIA_APU_XMA_CONTEXT_NEW_H_

#include "xenia/apu/xma_context.h"
#include "libretro/core_xma_owner_state.h"
#include "libretro/core_xma_native.h"
#include "xenia/base/bit_stream.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

struct kPacketInfo {
  uint8_t frame_count_;
  uint8_t current_frame_;
  uint32_t current_frame_size_;
  // First frame starting at or after the requested offset.
  uint32_t current_frame_offset_;

  const bool isLastFrameInPacket() const {
    return frame_count_ == 0 || current_frame_ == frame_count_ - 1;
  }
};

struct kPacketHandle {
  uint32_t buffer_index_ = 0;
  uint32_t packet_index_ = 0;
  bool is_valid_ = false;
};

static constexpr int kIdToSampleRate[4] = {24000, 32000, 44100, 48000};

class XmaContextNew : public XmaContext {
 public:
  static constexpr uint32_t kBitsPerPacketHeader = 32;
  static constexpr uint32_t kMaxFrameSizeinBits = 0x4000 - kBitsPerPacketHeader;

  explicit XmaContextNew();
  ~XmaContextNew();

  int Setup(uint32_t id, Memory* memory, uint32_t guest_ptr);
  bool Work();

  void Enable();
  void Clear();
  void Disable();
  void Release();

  // Exclusive state owner only: pause worker AND synchronous guest callers.
  // Views borrow storage. Prepare has no live-owner mutation; stage is noncopyable
  // by contract and must be committed/discarded before either owner is destroyed.
  bool CaptureState(xe_xma_owner_view* out) const;
  bool PrepareState(const xe_xma_owner_view* view, xe_xma_owner_stage* stage) const;
  bool CanCommitState(const xe_xma_owner_stage* stage) const;
  bool CommitState(xe_xma_owner_stage* stage);
  static void DiscardState(xe_xma_owner_stage* stage);

 private:
  friend class XmaDecoder;
  // Only after all owners preflight under the same exclusive state operation.
  void CommitPreparedState(xe_xma_owner_stage* stage);
  friend struct XmaContextWritebackTest;
  friend struct XmaContextStreamTest;
  void ClearLocked(XMA_CONTEXT_DATA* data);
  static void SwapInputBuffer(XMA_CONTEXT_DATA* data);
  // Convert sampling rate from ID to frequency.
  static int GetSampleRate(int id);
  // Get the containing packet number of the frame pointed to by the offset.
  static int16_t GetPacketNumber(size_t size, size_t bit_offset);

  const kPacketInfo GetPacketInfo(uint8_t* packet, uint32_t frame_offset);

  const uint32_t GetAmountOfBitsToRead(const uint32_t remaining_stream_bits,
                                       const uint32_t frame_size);

  kPacketHandle GetPacketHandle(XMA_CONTEXT_DATA* data, uint32_t buffer_index,
                                uint32_t packet_index,
                                uint32_t current_input_packet_count);

  const uint8_t* GetNextPacket(XMA_CONTEXT_DATA* data,
                               uint32_t next_packet_index,
                               uint32_t current_input_packet_count);

  // Single-buffer version of GetNextPacketReadOffset: scans `buffer`
  // from start_packet_index for the first packet whose frame_offset
  // header is valid (<= kMaxFrameSizeinBits).
  // Returns the bit offset of that frame in the buffer,
  // Or kBitsPerPacketHeader when no valid frame is found.
  const uint32_t GetNextPacketReadOffset(uint8_t* buffer,
                                         uint32_t next_packet_index,
                                         uint32_t current_input_packet_count);

  const uint32_t GetNextPacketReadOffset(XMA_CONTEXT_DATA* data,
                                         uint32_t next_packet_index,
                                         uint32_t current_input_packet_count);

  // Returns currently used buffer
  uint8_t* GetCurrentInputBuffer(XMA_CONTEXT_DATA* data);

  static uint32_t GetCurrentInputBufferSize(XMA_CONTEXT_DATA* data);

  void Decode(XMA_CONTEXT_DATA* data);
  void Consume(RingBuffer* XE_RESTRICT output_rb,
               const XMA_CONTEXT_DATA* const XE_RESTRICT data);

  void UpdateLoopStatus(XMA_CONTEXT_DATA* data);
  int PrepareDecoder(int sample_rate, bool is_two_channel);

  RingBuffer PrepareOutputRingBuffer(XMA_CONTEXT_DATA* data);

  // Re-reads context from guest memory and merges only decoder-owned fields,
  // preserving any game modifications made during decoding.
  void StoreContextMerged(const XMA_CONTEXT_DATA& data,
                          const XMA_CONTEXT_DATA& initial_data,
                          uint8_t* context_ptr);

  uint8_t raw_frame_[kBytesPerFrameChannel * 2];

  int32_t remaining_subframe_blocks_in_output_buffer_ = 0;
  uint8_t current_frame_remaining_subframes_ = 0;

  // Loop subframe precision state.
  // Maximum subframe blocks to output from the current frame (loop end
  // truncation).  0 means no limit.
  uint8_t loop_frame_output_limit_ = 0;
  // When true, the next decoded frame should skip leading subframes per
  // loop_subframe_skip (loop start adjustment).
  bool loop_start_skip_pending_ = false;
  // Decoded frame length in 256-byte blocks; guest settings may change before drain.
  uint8_t current_frame_total_subframes_ = 0;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_CONTEXT_H_
