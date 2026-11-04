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
  explicit CopyInput(size_t value) : id(value), copies(0), moves(0) {}
  CopyInput(const CopyInput& value) noexcept
      : id(value.id), copies(value.copies + 1), moves(value.moves) {}
  CopyInput(CopyInput&& value) noexcept
      : id(value.id), copies(value.copies), moves(value.moves + 1) {}
};
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL adapter line %u\n", unsigned(__LINE__)); std::abort(); } } while (0)
int main() {
  size_t expected = 0, callbacks = 0, discarded = 0;
  {
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> uninitialized;
    uninitialized.Shutdown([](const Input&, size_t) { std::abort(); });
  }
  {
    xe::gpu::PipelineCreationQueue<Input, size_t, unsigned> queue;
    queue.Initialize("Pipeline test",
        [](const Input& input, unsigned* context) {
          CHECK(input.owned && *input.owned == input.id);
          CHECK(!context || *context == 1234);
          return input.id == 17 ? size_t(0) : input.id + 1;
        },
        [&](const Input& input, size_t handle) {
          CHECK(input.id == expected++);
          CHECK(handle == (input.id == 17 ? size_t(0) : input.id + 1));
          CHECK(input.owned && *input.owned == input.id);
        }, []() { return std::make_unique<unsigned>(1234); });
    CHECK(!queue.has_threads());
    queue.SetThreadCount(2);
    for (size_t i = 0; i < 12000; ++i) {
      queue.Push(Input(i));
      if (i == 127) queue.SetThreadCount(6);
    }
    std::function<void()> callback = [&]() { ++callbacks; };
    queue.TakeCompletionCallback(callback);
    if (callback) callback();
    queue.DrainOnCallingThread(nullptr);
    queue.AwaitCompletion();
    queue.SetThreadCount(0);
    CHECK(expected == 12000 && callbacks == 1 && !queue.IsBusy());
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(discarded == 0);
    /* Cancel queued move-only requests and drop the callback, then reuse. */
    for (size_t i = 0; i < 10; ++i) queue.PushUnordered(Input(i));
    callback = [&]() { ++callbacks; };
    queue.TakeCompletionCallback(callback);
    CHECK(!callback);
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(callbacks == 1 && discarded == 0);
    queue.SetThreadCount(3);
    for (size_t i = 12000; i < 12100; ++i) queue.Push(Input(i));
    queue.AwaitCompletion();
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(expected == 12100 && discarded == 0);
    for (size_t round = 0; round < 32; ++round) {
      queue.SetThreadCount(3);
      size_t first = 12100 + round * 64;
      for (size_t i = first; i < first + 32; ++i) queue.Push(Input(i));
      callback = [&]() { ++callbacks; };
      queue.TakeCompletionCallback(callback);
      if (callback) callback();
      for (size_t i = first + 32; i < first + 64; ++i) queue.Push(Input(i));
      queue.AwaitCompletion();
      queue.Shutdown([&](const Input&, size_t) { ++discarded; });
      CHECK(expected == first + 64 && callbacks == round + 2 && discarded == 0);
    }
    size_t first = expected;
    queue.Initialize("Reinitialized pool",
        [](const Input& input, unsigned*) { return input.id + 1; },
        [&](const Input& input, size_t handle) {
          CHECK(input.id == expected++ && handle == input.id + 1);
        }, {});
    queue.SetThreadCount(2);
    for (size_t i = first; i < first + 100; ++i) queue.Push(Input(i));
    queue.AwaitCompletion();
    queue.Shutdown([&](const Input&, size_t) { ++discarded; });
    CHECK(expected == first + 100 && discarded == 0);
  }
  {
    size_t next = 0;
    unsigned seen[1000] = {};
    xe::gpu::PipelineCreationQueue<CopyInput, size_t, unsigned> queue;
    queue.Initialize("Copy budget",
        [](const CopyInput& input, unsigned*) {
          CHECK(input.copies == 1 && input.moves == 0);
          return input.id + 1;
        }, [&](const CopyInput& input, size_t handle) {
          CHECK(handle == input.id + 1 && input.copies == 1);
          if (input.id < 1000) {
            CHECK(input.id == next++ && input.moves == 1);
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
    queue.Shutdown([](const CopyInput&, size_t) { std::abort(); });
    CHECK(next == 1000);
    for (size_t i = 0; i < 1000; ++i) CHECK(seen[i] == 1);
  }
  std::puts("PASS: production creation queue ordered moves, failed creation, caller drain, context ownership, callbacks, cancellation and reuse");
}
