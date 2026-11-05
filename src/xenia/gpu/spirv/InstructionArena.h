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
  explicit InstructionArena(size_t reserve_records = 2048) {
    if (!xe_spv_arena_reserve(&arena_, reserve_records)) {
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
