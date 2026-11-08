/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_PIPELINE_CREATION_QUEUE_H_
#define XENIA_GPU_PIPELINE_CREATION_QUEUE_H_

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <rthreads/rthreads.h>
#include "libretro/core_job_pool.h"
#include "libretro/core_completion.h"

namespace xe {
namespace gpu {

// C++ object/callback lifetime glue; request storage, worker ownership, parking
// and completion accounting live in the C89 pool.
template <typename TRequest, typename THandle, typename TThreadContext>
class PipelineCreationQueue {
 public:
  using Creator = THandle (*)(void*, const TRequest&, TThreadContext*);
  using Store = void (*)(void*, const TRequest&, THandle);
  using ContextFactory = TThreadContext* (*)(void*);

  PipelineCreationQueue() { xe_completion_init(&completion_); }
  ~PipelineCreationQueue() {
    ReleasePending();
    xe_job_pool_free(pool_);
  }
  // The owner is borrowed until workers join; no callback owns a capture.
  void Initialize(const char* thread_name, void* opaque, Creator creator,
                  Store store, ContextFactory context_factory) {
    // Reinitialization follows Shutdown; release the previous cold owners.
    ReleasePending();
    xe_job_pool_free(pool_);
    pool_ = nullptr;
    xe_completion_cancel(&completion_);
    static_assert(alignof(Request) <= alignof(std::max_align_t));
    static_assert(std::is_nothrow_move_constructible<TRequest>::value);
    static_assert(std::is_nothrow_destructible<TRequest>::value);
    opaque_ = opaque;
    creator_ = creator;
    store_ = store;
    context_factory_ = context_factory;
    pool_ = xe_job_pool_new(4096, sizeof(Request), thread_name, &Run, &Discard,
        &NewContext, &FreeContext, &Idle, this);
    if (!pool_) throw std::bad_alloc();
  }

  // Join creators before the backend destroys entries. Every completed handle
  // already belongs to its entry; only unstarted requests need cancellation.
  void Shutdown() {
    if (!pool_) return;
    SetThreadCount(0);
    xe_completion_cancel(&completion_);
    xe_job_pool_arm_idle(pool_, 0);
    xe_job_pool_clear(pool_);
  }
  bool has_threads() const { return thread_count() != 0; }
  size_t thread_count() const { return xe_job_pool_threads(pool_); }
  void SetThreadCount(size_t count) {
    if (!pool_ && !count) return;
    if (!xe_job_pool_set_threads(pool_, count)) throw std::bad_alloc();
  }
  void Push(const TRequest& request) { PushRequest(request); }
  void Push(TRequest&& request) { PushRequest(std::move(request)); }
  bool IsBusy() { return has_threads() && xe_job_pool_busy(pool_); }
  void Notify() { xe_job_pool_notify(pool_); }
  void AwaitCompletion() {
    if (has_threads()) xe_job_pool_await(pool_);
  }
  void AwaitPending(xe_job_pending pending, const void* state) {
    if (has_threads()) xe_job_pool_await_pending(pool_, pending, state);
  }
  // Sole producer arms after its startup batch. No captured callable crosses
  // threads; only an atomic generation-tagged token is completed by workers.
  uint64_t WatchCompletion() {
    uint64_t ticket=xe_completion_begin(&completion_);
    if (!ArmCompletion(this)) xe_completion_finish(&completion_,ticket);
    return ticket;
  }
  bool CompletionPending(uint64_t ticket) const {
    return xe_completion_pending(&completion_,ticket)!=0;
  }
  void DrainOnCallingThread(TThreadContext* context) {
    while (xe_job_pool_run_one(pool_, context)) {}
  }

 private:
  struct Request {
    TRequest request;
    template <typename U>
    explicit Request(U&& input) noexcept
        : request(std::forward<U>(input)) {
      static_assert(std::is_nothrow_constructible<TRequest, U&&>::value);
    }
  };
  template <typename U>
  void PushRequest(U&& request) {
    // The GPU thread (or paused initialization) is the sole producer. Wait for
    // reusable storage without holding any lock a compilation worker needs.
    void* slot = xe_job_pool_begin(pool_);
    new (slot) Request(std::forward<U>(request));
    xe_job_pool_commit(pool_);
  }
  void ReleasePending() {
    if (!pool_) return;
    xe_job_pool_set_threads(pool_, 0);
    xe_completion_cancel(&completion_);
    xe_job_pool_arm_idle(pool_, 0);
  }
  static void Run(void* opaque, void* payload, void* context, uint64_t lease) {
    auto* self = static_cast<PipelineCreationQueue*>(opaque);
    auto* job = static_cast<Request*>(payload);
    THandle handle = self->creator_(self->opaque_, job->request,
                                  static_cast<TThreadContext*>(context));
    // Each entry publishes independently. Store borrows the original request;
    // the slot is released only after store and destruction have both finished.
    // No access to job is allowed after finish makes this slot reusable.
    self->store_(self->opaque_, job->request, handle);
    job->~Request();
    xe_job_pool_finish(self->pool_, lease);
  }
  static void Discard(void*, void* payload) {
    static_cast<Request*>(payload)->~Request();
  }
  static void* NewContext(void* opaque) {
    auto* self = static_cast<PipelineCreationQueue*>(opaque);
    return self->context_factory_ ? self->context_factory_(self->opaque_) : nullptr;
  }
  static void FreeContext(void*, void* context) {
    delete static_cast<TThreadContext*>(context);
  }
  static bool ArmCompletion(PipelineCreationQueue* self) {
    xe_job_pool_arm_idle(self->pool_, 1);
    if (xe_job_pool_busy(self->pool_)) return true;
    xe_job_pool_arm_idle(self->pool_, 0);
    return false;
  }
  static void Idle(void* opaque) {
    auto* self = static_cast<PipelineCreationQueue*>(opaque);
    uint64_t ticket=xe_completion_current(&self->completion_);
    if ((ticket&1) && !ArmCompletion(self))
      xe_completion_finish(&self->completion_,ticket);
  }
  void* opaque_ = nullptr;
  Creator creator_ = nullptr;
  Store store_ = nullptr;
  ContextFactory context_factory_ = nullptr;
  xe_completion completion_;
  xe_job_pool* pool_ = nullptr;
};

}  // namespace gpu
}  // namespace xe
#endif  // XENIA_GPU_PIPELINE_CREATION_QUEUE_H_
