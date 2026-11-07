/* Object/callback interoperability test for the production C89 worker pool. */
#include "xenia/gpu/pipeline_creation_queue.h"
#include <cstdio>
#include <cstdlib>
struct Input {
  size_t id;
  std::unique_ptr<size_t> owned;
  explicit Input(size_t value) : id(value), owned(new size_t(value)) {}
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
int main() {
  size_t expected = 0, discarded = 0;
  {
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> uninitialized;
    uninitialized.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(discarded == 0);
  }
  {
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> queue;
    struct Owner { size_t* expected; unsigned magic; } owner = { &expected, 1234 };
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
          size_t& expected = *owner->expected;
          CHECK(input.id == expected++);
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
    CHECK(expected == 12000 && !queue.CompletionPending(completion) && !queue.IsBusy());
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(discarded == 0);
    /* Cancel queued move-only requests and invalidate the token, then reuse. */
    for (size_t i = 0; i < 10; ++i) queue.PushUnordered(Input(i));
    completion = queue.WatchCompletion();
    CHECK(queue.CompletionPending(completion));
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(!queue.CompletionPending(completion) && discarded == 0);
    queue.SetThreadCount(3);
    for (size_t i = 12000; i < 12100; ++i) queue.Push(Input(i));
    queue.AwaitCompletion();
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(expected == 12100 && discarded == 0);
    for (size_t round = 0; round < 32; ++round) {
      queue.SetThreadCount(3);
      size_t first = 12100 + round * 64;
      for (size_t i = first; i < first + 32; ++i) queue.Push(Input(i));
      completion = queue.WatchCompletion();
      for (size_t i = first + 32; i < first + 64; ++i) queue.Push(Input(i));
      queue.AwaitCompletion();
      size_t completion_spins=0;
      while(queue.CompletionPending(completion)) CHECK(++completion_spins<100000000);
      queue.Shutdown([&](const Input&, size_t) { ++discarded; });
      CHECK(expected == first + 64 && !queue.CompletionPending(completion) && discarded == 0);
    }
    for (size_t round=0;round<20000;++round) {
      queue.SetThreadCount(3);
      queue.Push(Input(expected));
      uint64_t stale=queue.WatchCompletion();
      completion=queue.WatchCompletion();
      CHECK(!queue.CompletionPending(stale));
      size_t spins=0;
      while(queue.CompletionPending(completion)) CHECK(++spins<100000000);
      queue.AwaitCompletion();
    }
    size_t first = expected;
    queue.Initialize("Reinitialized pool", &expected,
        [](void*, const Input& input, unsigned*) { return input.id + 1; },
        [](void* opaque, const Input& input, size_t handle) {
          size_t& expected = *static_cast<size_t*>(opaque);
          CHECK(input.id == expected++ && handle == input.id + 1);
        }, {});
    queue.SetThreadCount(2);
    for (size_t i = first; i < first + 100; ++i) queue.Push(Input(i));
    queue.AwaitCompletion();
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(expected == first + 100 && discarded == 0);
  }
  {
    size_t next = 0, unexpected = 0;
    unsigned seen[1000] = {};
    xe::gpu::PipelineCreationQueue<CopyInput, size_t, unsigned> queue;
    struct CopyState { size_t* next; unsigned* seen; } state = { &next, seen };
    queue.Initialize("Copy budget", &state,
        [](void*, const CopyInput& input, unsigned*) {
          CHECK(input.copies == 1 && input.moves == 0 && input.identity == &input);
          return input.id + 1;
        }, [](void* opaque, const CopyInput& input, size_t handle) {
          auto* state = static_cast<CopyState*>(opaque);
          size_t& next = *state->next;
          unsigned* seen = state->seen;
          CHECK(handle == input.id + 1 && input.copies == 1 && input.identity == &input);
          if (input.id < 1000) {
            CHECK(input.id == next++ && input.moves == 0);
          } else {
            CHECK(input.id < 2000 && input.moves == 0);
            seen[input.id - 1000] = 1;
          }
        }, {});
    queue.SetThreadCount(4);
    for (size_t i = 0; i < 1000; ++i) {
      CopyInput input(i);
      queue.Push(input);
    }
    queue.AwaitCompletion();
    for (size_t i = 1000; i < 2000; ++i) {
      CopyInput input(i);
      queue.PushUnordered(input);
    }
    queue.AwaitCompletion();
    queue.Shutdown([&](const CopyInput&, size_t) { ++unexpected; });
    CHECK(next == 1000 && unexpected == 0);
    for (size_t i = 0; i < 1000; ++i) CHECK(seen[i] == 1);
  }
  {
    slock_t* blocked = slock_new();
    slock_t* state_lock = slock_new();
    scond_t* changed = scond_new();
    unsigned ordered_stored = 0, warm_stored = 0, unexpected = 0;
    CHECK(blocked && state_lock && changed);
    slock_lock(blocked);
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> queue;
    struct State { slock_t *blocked, *lock; scond_t* changed; unsigned *ordered, *warm; };
    State state = { blocked, state_lock, changed, &ordered_stored, &warm_stored };
    queue.Initialize("Independent warmup", &state, [](void* opaque, const Input& input, unsigned*) {
      auto* state = static_cast<State*>(opaque);
      if (!input.id) { slock_lock(state->blocked); slock_unlock(state->blocked); }
      return input.id + 1;
    }, [](void* opaque, const Input& input, size_t handle) {
      auto* state = static_cast<State*>(opaque);
      CHECK(handle == input.id + 1 && input.owned);
      slock_lock(state->lock);
      if (input.id) *state->ordered = 1;
      else { CHECK(*state->ordered); *state->warm = 1; }
      scond_signal(state->changed);
      slock_unlock(state->lock);
    }, {});
    queue.SetThreadCount(2);
    queue.PushUnordered(Input(0));
    queue.Push(Input(1));
    slock_lock(state_lock);
    while (!ordered_stored)
      CHECK(scond_wait_timeout(changed, state_lock, 5000000));
    CHECK(!warm_stored);
    slock_unlock(state_lock);
    slock_unlock(blocked);
    queue.AwaitCompletion();
    queue.Shutdown([&](const Input&, size_t) { ++unexpected; });
    CHECK(warm_stored && !unexpected);
    scond_free(changed); slock_free(state_lock); slock_free(blocked);
  }
  std::puts("PASS: production creation queue ordered moves, failed creation, caller drain, context ownership, completion tokens, cancellation and reuse");
}
