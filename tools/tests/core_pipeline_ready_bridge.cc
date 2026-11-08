/* Object lifetime interoperability only; scheduling is the production C89 pool. */
#include "xenia/gpu/pipeline_creation_queue.h"
#include "libretro/core_pipeline_publication.h"
#include "core_pipeline_ready_test.h"
#include "xenia/ui/gpu_completion_timeline.h"
class ReadyTimeline : public xe::ui::GPUCompletionTimeline {
 public:
  void Advance() { IncrementUpcomingSubmission(); }
  void UpdateCompletedSubmission() override {}
 private:
  void AwaitSubmissionImpl(uint64_t) override {}
};
extern "C" void* xe_ready_timeline_new() { return new ReadyTimeline; }
extern "C" void xe_ready_timeline_free(void* value) {
  delete static_cast<ReadyTimeline*>(value);
}
extern "C" void xe_ready_timeline_advance(void* value) {
  static_cast<ReadyTimeline*>(value)->Advance();
}
extern "C" uint64_t xe_ready_timeline_value(void* value) {
  return static_cast<ReadyTimeline*>(value)->GetUpcomingSubmission();
}
struct ReadyFixture {
  xe_ready_callbacks callbacks;
  xe::gpu::PipelineCreationQueue<size_t, uintptr_t, unsigned> queue;
};
extern "C" void* xe_ready_new(const xe_ready_callbacks* callbacks) {
  auto* fixture = new ReadyFixture;
  fixture->callbacks = *callbacks;
  fixture->queue.Initialize("Ready pipeline test", &fixture->callbacks,
      [](void* opaque, const size_t& id, unsigned*) {
        auto* cb = static_cast<xe_ready_callbacks*>(opaque);
        return cb->create(cb->owner, id);
      }, [](void* opaque, const size_t& id, uintptr_t handle) {
        auto* cb = static_cast<xe_ready_callbacks*>(opaque);
        cb->store(cb->owner, id, handle);
      }, nullptr);
  return fixture;
}
extern "C" void xe_ready_free(void* value) {
  delete static_cast<ReadyFixture*>(value);
}
extern "C" void xe_ready_push(void* value, size_t id) {
  static_cast<ReadyFixture*>(value)->queue.Push(id);
}
extern "C" void xe_ready_threads(void* value, size_t count) {
  static_cast<ReadyFixture*>(value)->queue.SetThreadCount(count);
}
template <typename Q> static void shutdown(Q& queue) {
  // The exact-parent negative control uses the same C driver and bridge.
  if constexpr (requires { queue.Shutdown(); }) queue.Shutdown();
  else queue.Shutdown([](const size_t&, uintptr_t) {});
}
extern "C" void xe_ready_shutdown(void* value) {
  shutdown(static_cast<ReadyFixture*>(value)->queue);
}
extern "C" void xe_ready_await(void* value) {
  static_cast<ReadyFixture*>(value)->queue.AwaitCompletion();
}
extern "C" int xe_ready_busy(void* value) {
  return static_cast<ReadyFixture*>(value)->queue.IsBusy();
}
extern "C" void xe_ready_await_target(void* value, const void* target) {
  static_cast<ReadyFixture*>(value)->queue.AwaitPending(
      &xe_pipeline_is_pending, target);
}
