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
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <rthreads/rthreads.h>
#include "libretro/core_job_pool.h"
#include "libretro/core_completion.h"
#include "xenia/gpu/pipeline_publish_order.h"

namespace xe {
namespace gpu {

// C++ object/callback lifetime glue; request storage, worker ownership, parking
// and completion accounting live in the C89 pool.
template <typename TRequest, typename THandle, typename TThreadContext>
class PipelineCreationQueue {
  struct Request;
 public:
  using Publication = std::pair<Request*, THandle>;
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
    publish_order_.Initialize([](void* opaque, Publication& publication) {
      auto* self = static_cast<PipelineCreationQueue*>(opaque);
      self->store_(self->opaque_, publication.first->request, publication.second);
      self->Release(publication.first);
    }, this);
    pool_ = xe_job_pool_new(4096, sizeof(Request), thread_name, &Run, &Discard,
        &NewContext, &FreeContext, &Idle, this);
    if (!pool_) throw std::bad_alloc();
  }

  // Preserve the backend's stop/join-before-destruction order. Queued requests
  // are cancelled; completed but unpublished handles are destroyed in place.
  template <typename F>
  void Shutdown(F&& discard) {
    if (!pool_) return;
    SetThreadCount(0);
    xe_completion_cancel(&completion_);
    xe_job_pool_arm_idle(pool_, 0);
    publish_order_.Reset([&](Publication& publication) {
      discard(publication.first->request, publication.second);
      Release(publication.first);
    });
    xe_job_pool_clear(pool_);
  }
  bool has_threads() const { return thread_count() != 0; }
  size_t thread_count() const { return xe_job_pool_threads(pool_); }
  void SetThreadCount(size_t count) {
    if (!pool_ && !count) return;
    if (!xe_job_pool_set_threads(pool_, count)) throw std::bad_alloc();
  }
  void Push(const TRequest& request) { PushRequest(request, true); }
  void Push(TRequest&& request) { PushRequest(std::move(request), true); }
  void PushUnordered(const TRequest& request) { PushRequest(request, false); }
  void PushUnordered(TRequest&& request) { PushRequest(std::move(request), false); }
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
    uint64_t sequence_or_lease;
    TRequest request;
    template <typename U>
    Request(size_t value, U&& input) noexcept
        : sequence_or_lease(value), request(std::forward<U>(input)) {
      static_assert(std::is_nothrow_constructible<TRequest, U&&>::value);
    }
  };
  template <typename U>
  void PushRequest(U&& request, bool ordered) {
    // The GPU thread (or paused initialization) is the sole producer. Wait for
    // reusable storage without holding any lock a compilation worker needs.
    size_t sequence = ordered ? publish_order_.NextSequence() : 0;
    void* slot = xe_job_pool_begin(pool_);
    new (slot) Request(sequence, std::forward<U>(request));
    xe_job_pool_commit(pool_);
  }
  void Release(Request* job) {
    uint64_t lease = job->sequence_or_lease;
    job->~Request();
    xe_job_pool_finish(pool_, lease);
  }
  void ReleasePending() {
    if (!pool_) return;
    xe_job_pool_set_threads(pool_, 0);
    xe_completion_cancel(&completion_);
    xe_job_pool_arm_idle(pool_, 0);
    publish_order_.Reset([this](Publication& publication) {
      Release(publication.first);
    });
  }
  static void Run(void* opaque, void* payload, void* context, uint64_t lease) {
    auto* self = static_cast<PipelineCreationQueue*>(opaque);
    auto* job = static_cast<Request*>(payload);
    size_t sequence = size_t(job->sequence_or_lease);
    job->sequence_or_lease = lease;
    THandle handle = self->creator_(self->opaque_, job->request,
                                  static_cast<TThreadContext*>(context));
    // Publication owns the borrowed request until store/discard. No access to
    // job is allowed after either path releases its lease.
    if (sequence) self->publish_order_.Publish(sequence, job, handle);
    else {
      self->store_(self->opaque_, job->request, handle);
      self->Release(job);
    }
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
  PipelinePublishOrder<Publication> publish_order_;
  xe_completion completion_;
  xe_job_pool* pool_ = nullptr;
};

}  // namespace gpu
}  // namespace xe
#endif  // XENIA_GPU_PIPELINE_CREATION_QUEUE_H_
