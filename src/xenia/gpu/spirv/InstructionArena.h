/* Typed lifetime bridge only; allocation and reuse live in the C89 arena. */
#ifndef XENIA_GPU_SPIRV_INSTRUCTION_ARENA_H_
#define XENIA_GPU_SPIRV_INSTRUCTION_ARENA_H_
#include <cassert>
#include <new>
#include "libretro/core_spv_arena.h"

namespace spv {
// Declare before every builder and detached instruction owner borrowing it.
// One compiler owns this workspace; it is never shared between workers.
class InstructionArena final {
 public:
  explicit InstructionArena(size_t reserve_records = 2048,
      size_t block_bytes = 0, size_t function_bytes = 0) {
    if (!xe_spv_arena_reserve(&arena_, reserve_records) ||
        !xe_spv_arena_reserve_payloads(&arena_, 16384) ||
        !xe_spv_arena_reserve_constants(&arena_, 128) ||
        !xe_spv_arena_reserve_type_groups(&arena_) ||
        !xe_spv_arena_reserve_extensions(&arena_, XE_SPV_EXTENSION_COLD_CAPACITY) ||
        !xe_spv_arena_reserve_decorations(&arena_, XE_SPV_DECORATION_COLD_CAPACITY) ||
        !xe_spv_arena_reserve_stacks(&arena_, sizeof(void*) * 4) ||
        !xe_spv_arena_reserve_capabilities(&arena_, 64) ||
        ((block_bytes || function_bytes) &&
         !xe_spv_arena_reserve_objects(&arena_, block_bytes, 64, function_bytes, 32))) {
      xe_spv_arena_close(&arena_);
      throw std::bad_alloc();
    }
  }
  ~InstructionArena() {
    const int closed = xe_spv_arena_close(&arena_);
    assert(closed);
    (void)closed;
  }
  InstructionArena(const InstructionArena&) = delete;
  InstructionArena& operator=(const InstructionArena&) = delete;
  xe_spv_arena* get() { return &arena_; }

 private:
  xe_spv_arena arena_ = {};
};
}  // namespace spv
#endif
