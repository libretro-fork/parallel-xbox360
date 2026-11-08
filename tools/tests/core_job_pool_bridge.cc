/* Object/callback interoperability test for the production C89 worker pool. */
#include "xenia/gpu/pipeline_creation_queue.h"
#include "libretro/core_pipeline_publication.h"
#include "core_pipeline_ready_test.h"
#include <memory>
#include <cstdio>
#include <cstdlib>
static retro_atomic_int_t owned_created, owned_freed;
struct Input {
  size_t id;
  std::unique_ptr<size_t> owned;
  explicit Input(size_t value) : id(value), owned(new size_t(value)) {
    retro_atomic_fetch_add_int(&owned_created, 1);
  }
  ~Input() { if (owned) retro_atomic_fetch_add_int(&owned_freed, 1); }
  Input(Input&&) noexcept = default;
  Input(const Input&) = delete;
};
struct CopyInput {
  size_t id;
  unsigned copies, moves;
  const CopyInput* identity;
  explicit CopyInput(size_t value) : id(value), copies(0), moves(0), identity(this) {}
  CopyInput(const CopyInput& value) noexcept
      : id(value.id), copies(value.copies + 1), moves(value.moves), identity(this) {}
  CopyInput(CopyInput&& value) noexcept
      : id(value.id), copies(value.copies), moves(value.moves + 1), identity(this) {}
};
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL adapter line %u\n", unsigned(__LINE__)); std::abort(); } } while (0)
template <typename Q, typename F> static void shutdown(Q& queue, F&& discard) {
  if constexpr (requires { queue.Shutdown(); }) queue.Shutdown();
  else queue.Shutdown(std::forward<F>(discard));
}
int main() {
  retro_atomic_int_init(&owned_created, 0);
  retro_atomic_int_init(&owned_freed, 0);
  retro_atomic_int_t expected;
  retro_atomic_int_init(&expected, 0);
  size_t discarded = 0;
  {
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> uninitialized;
    shutdown(uninitialized, [&](const Input&, size_t) { ++discarded; });
    CHECK(discarded == 0);
  }
  {
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> queue;
    struct Owner { retro_atomic_int_t* expected; unsigned magic; } owner = { &expected, 1234 };
    queue.Initialize("Pipeline test", &owner,
        [](void* opaque, const Input& input, unsigned* context) {
          CHECK(static_cast<Owner*>(opaque)->magic == 1234);
          CHECK(input.owned && *input.owned == input.id);
          CHECK(!context || *context == 1234);
          return input.id == 17 ? size_t(0) : input.id + 1;
        },
        [](void* opaque, const Input& input, size_t handle) {
          auto* owner = static_cast<Owner*>(opaque);
          CHECK(owner->magic == 1234);
          retro_atomic_fetch_add_int(owner->expected, 1);
          CHECK(handle == (input.id == 17 ? size_t(0) : input.id + 1));
          CHECK(input.owned && *input.owned == input.id);
        }, [](void* opaque) {
          CHECK(static_cast<Owner*>(opaque)->magic == 1234);
          return new unsigned(1234);
        });
    CHECK(!queue.has_threads());
    queue.SetThreadCount(2);
    for (size_t i = 0; i < 12000; ++i) {
      queue.Push(Input(i));
      if (i == 127) queue.SetThreadCount(6);
    }
    uint64_t completion = queue.WatchCompletion();
    queue.DrainOnCallingThread(nullptr);
    queue.AwaitCompletion();
    queue.SetThreadCount(0);
    CHECK(retro_atomic_load_acquire_int(&expected) == 12000 && !queue.CompletionPending(completion) && !queue.IsBusy());
    shutdown(queue, [&](const Input&, size_t) { ++discarded; });
    CHECK(discarded == 0);
    /* Cancel queued move-only requests and invalidate the token, then reuse. */
    for (size_t i = 0; i < 10; ++i) queue.Push(Input(i));
    completion = queue.WatchCompletion();
    CHECK(queue.CompletionPending(completion));
    shutdown(queue, [&](const Input&, size_t) { ++discarded; });
    CHECK(!queue.CompletionPending(completion) && discarded == 0);
    queue.SetThreadCount(3);
    for (size_t i = 12000; i < 12100; ++i) queue.Push(Input(i));
    queue.AwaitCompletion();
    shutdown(queue, [&](const Input&, size_t) { ++discarded; });
    CHECK(retro_atomic_load_acquire_int(&expected) == 12100 && discarded == 0);
    for (size_t round = 0; round < 32; ++round) {
      queue.SetThreadCount(3);
      size_t first = 12100 + round * 64;
      for (size_t i = first; i < first + 32; ++i) queue.Push(Input(i));
      completion = queue.WatchCompletion();
      for (size_t i = first + 32; i < first + 64; ++i) queue.Push(Input(i));
      queue.AwaitCompletion();
      size_t completion_spins=0;
      while(queue.CompletionPending(completion)) CHECK(++completion_spins<100000000);
      shutdown(queue, [&](const Input&, size_t) { ++discarded; });
      CHECK(retro_atomic_load_acquire_int(&expected) == first + 64 && !queue.CompletionPending(completion) && discarded == 0);
    }
    for (size_t round=0;round<20000;++round) {
      queue.SetThreadCount(3);
      queue.Push(Input(retro_atomic_load_acquire_int(&expected)));
      uint64_t stale=queue.WatchCompletion();
      completion=queue.WatchCompletion();
      CHECK(!queue.CompletionPending(stale));
      size_t spins=0;
      while(queue.CompletionPending(completion)) CHECK(++spins<100000000);
      queue.AwaitCompletion();
    }
    size_t first = size_t(retro_atomic_load_acquire_int(&expected));
    queue.Initialize("Reinitialized pool", &owner,
        [](void*, const Input& input, unsigned*) { return input.id + 1; },
        [](void* opaque, const Input& input, size_t handle) {
          retro_atomic_fetch_add_int(static_cast<Owner*>(opaque)->expected, 1);
          CHECK(handle == input.id + 1);
        }, {});
    queue.SetThreadCount(2);
    for (size_t i = first; i < first + 100; ++i) queue.Push(Input(i));
    queue.AwaitCompletion();
    shutdown(queue, [&](const Input&, size_t) { ++discarded; });
    CHECK(retro_atomic_load_acquire_int(&expected) == first + 100 && discarded == 0);
  }
  {
    size_t unexpected = 0;
    unsigned seen[2000] = {};
    xe::gpu::PipelineCreationQueue<CopyInput, size_t, unsigned> queue;
    struct CopyState { unsigned* seen; } state = { seen };
    queue.Initialize("Copy budget", &state,
        [](void*, const CopyInput& input, unsigned*) {
          CHECK(input.copies == 1 && input.moves == 0 && input.identity == &input);
          return input.id + 1;
        }, [](void* opaque, const CopyInput& input, size_t handle) {
          auto* state = static_cast<CopyState*>(opaque);
          unsigned* seen = state->seen;
          CHECK(handle == input.id + 1 && input.copies == 1 && input.identity == &input);
          CHECK(input.id < 2000 && input.moves == 0);
          CHECK(seen[input.id] == 0);
          seen[input.id] = 1;
        }, {});
    queue.SetThreadCount(4);
    for (size_t i = 0; i < 1000; ++i) {
      CopyInput input(i);
      queue.Push(input);
    }
    queue.AwaitCompletion();
    for (size_t i = 1000; i < 2000; ++i) {
      CopyInput input(i);
      queue.Push(input);
    }
    queue.AwaitCompletion();
    shutdown(queue, [&](const CopyInput&, size_t) { ++unexpected; });
    CHECK(unexpected == 0);
    for (size_t i = 0; i < 2000; ++i) CHECK(seen[i] == 1);
  }
  CHECK(retro_atomic_load_acquire_int(&owned_created) ==
        retro_atomic_load_acquire_int(&owned_freed));
  CHECK(xe_ready_test());
  std::puts("PASS: production creation queue borrowed moves, failed creation, caller drain, context ownership, completion tokens, cancellation and reuse");
}
