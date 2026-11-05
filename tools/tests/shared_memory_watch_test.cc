/* Typed SharedMemory interop; no GPU, frontend or guest memory mapping. */
#include "xenia/gpu/shared_memory.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
class TestSharedMemory final : public xe::gpu::SharedMemory {
 public:
  explicit TestSharedMemory(xe::Memory& memory) : SharedMemory(memory) { CHECK(InitializeCommon()); }
 private:
  bool UploadRanges(const std::pair<uint32_t, uint32_t>*, uint32_t) override { return false; }
};
struct watch_slot { xe::gpu::SharedMemory::WatchHandle value; unsigned hits; };
struct cancellation {
  TestSharedMemory* memory;
  watch_slot* slots;
  retro_atomic_int_t started;
};
static void invalidated(const xe::global_unique_lock_type&, void* context,
    void*, uint64_t, bool) {
  watch_slot* slot = (watch_slot*)context;
  CHECK(slot->value);
  slot->value = nullptr;
  ++slot->hits;
}
static void cancel(void* opaque) {
  cancellation* state = (cancellation*)opaque;
#ifdef XE_TEST_STALE_WATCH_VALUES
  /* The old destructor captured these before the callee took the region. */
  xe::gpu::SharedMemory::WatchHandle first = state->slots[0].value;
  xe::gpu::SharedMemory::WatchHandle second = state->slots[1].value;
#endif
  retro_atomic_store_release_int(&state->started, 1);
#ifdef XE_TEST_STALE_WATCH_VALUES
  state->memory->UnwatchMemoryRange(first);
  state->memory->UnwatchMemoryRange(second);
#else
  state->memory->UnwatchMemoryRanges(&state->slots[0].value, &state->slots[1].value);
#endif
}
int main(void) {
  xe::Memory memory;
  TestSharedMemory shared(memory);
  watch_slot old[2], fresh[4];
  cancellation state;
  sthread_t* thread;
  unsigned round, i, j, attempts;
  for (round = 0; round < 256; ++round) {
    for (i = 0; i < 2; ++i) { old[i].value = nullptr; old[i].hits = 0; }
    state.memory = &shared;
    state.slots = old;
    retro_atomic_store_relaxed_int(&state.started, 0);
    {
      xe::global_unique_lock_type region = xe::global_critical_region::Acquire();
      /* Both watches span bucket boundaries. */
      for (i = 0; i < 2; ++i)
        old[i].value = shared.WatchMemoryRange(0x3FF000 + i * 0x800000,
            0x402000, invalidated, &old[i], nullptr, i);
      thread = sthread_create(cancel, &state);
      CHECK(thread);
      attempts = 1000000000u;
      while (!retro_atomic_load_acquire_int(&state.started) && --attempts) {}
      CHECK(attempts);
      /* Deterministic invalidation between handle capture and lock acquisition. */
      shared.MemoryInvalidationCallback(0, 0x1800000, true);
      CHECK(!old[0].value && !old[1].value && old[0].hits == 1 && old[1].hits == 1);
    }
    sthread_join(thread);
    {
      xe::global_unique_lock_type region = xe::global_critical_region::Acquire();
      for (i = 0; i < 4; ++i) {
        fresh[i].hits = 0;
        fresh[i].value = shared.WatchMemoryRange(0x1000 + i * 0x800000,
            0x401000, invalidated, &fresh[i], nullptr, i);
        CHECK(fresh[i].value);
        for (j = 0; j < i; ++j) {
          if (fresh[i].value == fresh[j].value) {
            std::fprintf(stderr, "REPRO: stale cancellation handed out a live watch twice\n");
            std::exit(76); /* Isolated old-code regression process only. */
          }
        }
      }
#ifndef XE_TEST_STALE_WATCH_VALUES
      shared.UnwatchMemoryRanges(&fresh[0].value, &fresh[1].value);
      CHECK(!fresh[0].value && !fresh[1].value);
      shared.UnwatchMemoryRanges(&fresh[0].value, &fresh[1].value);
#endif
      shared.MemoryInvalidationCallback(0, 0x2000000, true);
      for (i = 0; i < 4; ++i) {
        CHECK(!fresh[i].value);
#ifndef XE_TEST_STALE_WATCH_VALUES
        CHECK(fresh[i].hits == (i < 2 ? 0u : 1u));
#endif
      }
    }
  }
#ifndef XE_TEST_STALE_WATCH_VALUES
  {
    xe::global_unique_lock_type region = xe::global_critical_region::Acquire();
    fresh[0].value = shared.WatchMemoryRange(0x1000, 0x1000, invalidated,
        &fresh[0], nullptr, 0);
    shared.UnwatchMemoryRanges(&fresh[0].value, &fresh[0].value);
    CHECK(!fresh[0].value);
  }
#endif
  std::puts("PASS: 256 forced invalidation/cancel races, multi-bucket reuse and repeated empty cancellation");
  return 0;
}
