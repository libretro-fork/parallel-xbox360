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
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <rthreads/rthreads.h>
#include "libretro/core_job_pool.h"
#include "xenia/gpu/pipeline_publish_order.h"

namespace xe {
namespace gpu {

// C++ object/callback lifetime glue; request storage, worker ownership, parking
// and completion accounting live in the C89 pool.
template <typename TRequest, typename THandle, typename TThreadContext>
class PipelineCreationQueue {
 public:
  using Publication = std::pair<TRequest, THandle>;
  using Creator = std::function<THandle(const TRequest&, TThreadContext*)>;
  using Store = std::function<void(const TRequest&, THandle)>;
  using ContextFactory = std::function<std::unique_ptr<TThreadContext>()>;

  ~PipelineCreationQueue() {
    xe_job_pool_free(pool_);
    slock_free(callback_lock_);
  }
  void Initialize(const char* thread_name, Creator creator, Store store,
                  ContextFactory context_factory) {
    // Reinitialization follows Shutdown; release the previous cold owners.
    xe_job_pool_free(pool_);
    pool_ = nullptr;
    slock_free(callback_lock_);
    callback_lock_ = nullptr;
    completion_callback_ = nullptr;
    static_assert(alignof(Request) <= alignof(std::max_align_t));
    static_assert(std::is_nothrow_move_constructible<TRequest>::value);
    static_assert(std::is_nothrow_destructible<TRequest>::value);
    creator_ = std::move(creator);
    store_ = std::move(store);
    context_factory_ = std::move(context_factory);
    publish_order_.Initialize([](void* opaque, Publication& publication) {
      auto* self = static_cast<PipelineCreationQueue*>(opaque);
      self->store_(publication.first, publication.second);
    }, this);
    callback_lock_ = slock_new();
    if (!callback_lock_) throw std::bad_alloc();
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
    slock_lock(callback_lock_);
    completion_callback_ = nullptr;
    xe_job_pool_arm_idle(pool_, 0);
    slock_unlock(callback_lock_);
    xe_job_pool_clear(pool_);
    publish_order_.Reset([&](Publication& publication) {
      discard(publication.first, publication.second);
    });
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
  void TakeCompletionCallback(std::function<void()>& callback) {
    slock_lock(callback_lock_);
    if (xe_job_pool_busy(pool_)) {
      completion_callback_ = std::move(callback);
      callback = nullptr;
      if (!ArmCompletion(this)) {
        callback = std::move(completion_callback_);
        completion_callback_ = nullptr;
      }
    }
    slock_unlock(callback_lock_);
  }
  void DrainOnCallingThread(TThreadContext* context) {
    while (xe_job_pool_run_one(pool_, context)) {}
  }

 private:
  struct Request {
    size_t sequence;
    TRequest request;
    template <typename U>
    Request(size_t value, U&& input) noexcept
        : sequence(value), request(std::forward<U>(input)) {
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
  static void Run(void* opaque, void* payload, void* context) {
    auto* self = static_cast<PipelineCreationQueue*>(opaque);
    auto* job = static_cast<Request*>(payload);
    THandle handle = self->creator_(job->request,
                                    static_cast<TThreadContext*>(context));
    if (job->sequence)
      self->publish_order_.Publish(job->sequence, std::move(job->request), handle);
    else
      self->store_(job->request, handle);
    job->~Request();
  }
  static void Discard(void*, void* payload) {
    static_cast<Request*>(payload)->~Request();
  }
  static void* NewContext(void* opaque) {
    auto* self = static_cast<PipelineCreationQueue*>(opaque);
    return self->context_factory_ ? self->context_factory_().release() : nullptr;
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
    std::function<void()> callback;
    slock_lock(self->callback_lock_);
    if (self->completion_callback_ && !ArmCompletion(self)) {
      callback = std::move(self->completion_callback_);
      self->completion_callback_ = nullptr;
    }
    slock_unlock(self->callback_lock_);
    if (callback) callback();
  }
  Creator creator_;
  Store store_;
  ContextFactory context_factory_;
  PipelinePublishOrder<Publication> publish_order_;
  std::function<void()> completion_callback_;
  slock_t* callback_lock_ = nullptr;
  xe_job_pool* pool_ = nullptr;
};

}  // namespace gpu
}  // namespace xe
#endif  // XENIA_GPU_PIPELINE_CREATION_QUEUE_H_
