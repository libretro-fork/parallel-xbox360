/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <forward_list>
#include <mutex>
#include <new>

#include "third_party/disruptorplus/include/disruptorplus/blocking_wait_strategy.hpp"
#include "third_party/disruptorplus/include/disruptorplus/multi_threaded_claim_strategy.hpp"
#include "third_party/disruptorplus/include/disruptorplus/ring_buffer.hpp"
#include "third_party/disruptorplus/include/disruptorplus/sequence_barrier.hpp"
#include "third_party/disruptorplus/include/disruptorplus/spin_wait.hpp"
#include "third_party/disruptorplus/include/disruptorplus/spin_wait_strategy.hpp"
#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/base/threading_timer_queue.h"
#include "libretro/core_pause_gate.h"
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace dp = disruptorplus;

namespace xe {
namespace threading {

using WaitItem = TimerQueueWaitItem;
/*
        chrispy: changed this to a blocking wait from a spin-wait, the spin was
   monopolizing a ton of cpu time (depending on the game 2-4% of total cpu time)
   on my 3990x no complaints since that change
*/

/*
        edit: actually had to change it back, when i was testing it only worked
   because i fixed disruptorplus' code to compile (it gives wrong args to
   condition_variable::wait_until) but now builds

*/

/*
    edit2: (30.12.2024) After uplifting version of MSVC compiler Xenia cannot be
   correctly initialized if you're using proton.
*/
// Windows producers keep spinning: blocking waits deadlocked initialisation
// under Proton (c3301d928).
// The dispatch thread waits on Win32 handles instead, since spin waits sleep
// 1 ms at a time and fire timers 1-3 ms late.
#if XE_PLATFORM_WIN32
using WaitStrat = dp::spin_wait_strategy;
#else
using WaitStrat = dp::blocking_wait_strategy;
#endif

class TimerQueue {
 public:
  using clock = WaitItem::clock;
  static_assert(clock::is_steady);

 public:
  TimerQueue()
      : buffer_(kWaitCount),
        wait_strategy_(),
        claim_strategy_(kWaitCount, wait_strategy_),
        consumed_(wait_strategy_),
        shutdown_(0) {
    claim_strategy_.add_claim_barrier(consumed_);
    pause_gate_.reset(xe_pause_gate_new());
    if (!pause_gate_ || !retro_eventcount_init(&changed_)) throw std::bad_alloc();
#if XE_PLATFORM_WIN32
    wake_event_ = CreateEventW(nullptr, false, false, nullptr);
    if (!wake_event_) {
      retro_eventcount_free(&changed_);
      throw std::bad_alloc();
    }
    wait_timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
    if (!wait_timer_) {
      // Before Windows 10 1803.
      wait_timer_ =
          CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
#endif
    dispatch_thread_ = sthread_create([](void* context) {
      static_cast<TimerQueue*>(context)->TimerThreadMain();
    }, this);
    if (!dispatch_thread_) {
#if XE_PLATFORM_WIN32
      if (wait_timer_) CloseHandle(wait_timer_);
      CloseHandle(wake_event_);
#endif
      retro_eventcount_free(&changed_);
      throw std::bad_alloc();
    }
  }

  ~TimerQueue() {
    retro_atomic_store_release_int(&shutdown_, 1);
    xe_pause_gate_stop(pause_gate_.get());
    Wake();
    sthread_join(dispatch_thread_);
    dispatch_thread_ = nullptr;
    // A retained weak item's final owner may outlive queue shutdown. Its
    // terminal state must be visible before the parent queue is destroyed.
    for (auto& item : wait_queue_)
      retro_atomic_store_release_int(&item->state_, static_cast<int>(WaitItem::State::kDisarmed));
    for (size_t i = 0; i < kWaitCount; ++i)
      if (buffer_[i]) retro_atomic_store_release_int(&buffer_[i]->state_, static_cast<int>(WaitItem::State::kDisarmed));
    retro_eventcount_notify(&changed_);
#if XE_PLATFORM_WIN32
    CloseHandle(wait_timer_);
    CloseHandle(wake_event_);
#endif
    retro_eventcount_free(&changed_);
  }

  void TimerThreadMain() {
    dp::sequence_t next_sequence = 0;
    const auto comp = [](const std::shared_ptr<WaitItem>& left,
                         const std::shared_ptr<WaitItem>& right) {
      return left->due_ < right->due_;
    };

    xe::threading::set_name("xe::threading::TimerQueue");

    while (!retro_atomic_load_acquire_int(&shutdown_)) {
      if (!xe_pause_gate_wait_index(pause_gate_.get(), 0)) break;
      {
        // Consume new wait items and add them to sorted wait queue
        dp::sequence_t available = WaitUntilPublished(
            next_sequence, wait_queue_.empty() ? clock::now() + kIdleWait
                                               : wait_queue_.front()->due_);

        // Check for timeout
        if (available != next_sequence - 1) {
          std::forward_list<std::shared_ptr<WaitItem>> wait_items;
          do {
            wait_items.push_front(std::move(buffer_[next_sequence]));
          } while (next_sequence++ != available);

          consumed_.publish(available);

          wait_items.sort(comp);
          wait_queue_.merge(wait_items, comp);
        }
      }

      if (retro_atomic_load_relaxed_int(&cancelled_) &&
          retro_atomic_exchange_int(&cancelled_, 0)) {
        wait_queue_.remove_if([](const std::shared_ptr<WaitItem>& item) {
          return retro_atomic_load_acquire_int(&item->state_) == static_cast<int>(WaitItem::State::kDisarmed);
        });
      }
      if (xe_pause_gate_is_paused(pause_gate_.get()) ||
          retro_atomic_load_acquire_int(&shutdown_)) continue;

      {
        // Check wait queue, invoke callbacks and reschedule
        std::forward_list<std::shared_ptr<WaitItem>> wait_items;
        while (!wait_queue_.empty() && !xe_pause_gate_is_paused(pause_gate_.get()) &&
               !retro_atomic_load_acquire_int(&shutdown_) &&
               wait_queue_.front()->due_ <= clock::now()) {
          auto wait_item = std::move(wait_queue_.front());
          wait_queue_.pop_front();

          // Ensure that it isn't disarmed
          if (retro_atomic_cas_int(&wait_item->state_, static_cast<int>(WaitItem::State::kIdle),
                                   static_cast<int>(WaitItem::State::kInCallback))) {
            // Possibility to dispatch to a thread pool here
            assert_not_null(wait_item->callback_);
            wait_item->callback_(wait_item->userdata_);

            if (wait_item->interval_ != clock::duration::zero() &&
                retro_atomic_load_acquire_int(&wait_item->state_) !=
                    static_cast<int>(WaitItem::State::kInCallbackSelfDisarmed)) {
              // Item is recurring and didn't self-disarm during callback:
              wait_item->due_ += wait_item->interval_;
              retro_atomic_store_release_int(&wait_item->state_, static_cast<int>(WaitItem::State::kIdle));
              wait_items.push_front(std::move(wait_item));
            } else {
              retro_atomic_store_release_int(&wait_item->state_, static_cast<int>(WaitItem::State::kDisarmed));
            }
          } else {
            // Specifically, kInCallback is illegal here
            assert_true(static_cast<int>(WaitItem::State::kDisarmed) == retro_atomic_load_acquire_int(&wait_item->state_));
          }
          retro_eventcount_notify(&changed_);
        }
        wait_items.sort(comp);
        wait_queue_.merge(wait_items, comp);
      }
    }
  }

  std::weak_ptr<WaitItem> QueueTimer(std::shared_ptr<WaitItem> wait_item) {
    auto wait_item_weak = std::weak_ptr<WaitItem>(wait_item);

    // Mitigate callback flooding
    wait_item->due_ =
        std::max(clock::now() - wait_item->interval_, wait_item->due_);

    auto sequence = claim_strategy_.claim_one();
    buffer_[sequence] = std::move(wait_item);
    claim_strategy_.publish(sequence);
    Wake();

    return wait_item_weak;
  }

  bool IsDispatchThread() const { return dispatch_thread_ && sthread_isself(dispatch_thread_); }
  void Wake() {
    retro_eventcount_notify(&changed_);
#if XE_PLATFORM_WIN32
    if (!IsDispatchThread()) SetEvent(wake_event_);
#endif
  }
  void Cancelled() {
    retro_atomic_store_release_int(&cancelled_, 1);
    Wake();
  }
  void WaitForCallback(WaitItem* item) {
    const int key = retro_eventcount_prepare_wait(&changed_);
    const int state = retro_atomic_load_acquire_int(&item->state_);
    if (state == static_cast<int>(WaitItem::State::kInCallback) ||
        state == static_cast<int>(WaitItem::State::kInCallbackSelfDisarmed))
      retro_eventcount_commit_wait(&changed_, key);
    else retro_eventcount_cancel_wait(&changed_);
  }
  bool PauseForState(int64_t timeout_us) {
    if (IsDispatchThread()) return false;
    xe_pause_gate_set(pause_gate_.get(), 1);
    Wake();
    if (xe_pause_gate_wait_workers(pause_gate_.get(), 1, timeout_us)) return true;
    ResumeFromState();
    return false;
  }
  void ResumeFromState() { xe_pause_gate_set(pause_gate_.get(), 0); Wake(); }

 private:
  // Returns the last published sequence, or next_sequence - 1 if nothing new
  // was published.
  dp::sequence_t WaitUntilPublished(dp::sequence_t next_sequence,
                                    clock::time_point timeout) {
#if XE_PLATFORM_WIN32
    dp::sequence_t available =
        claim_strategy_.last_published_after(next_sequence - 1);
    if (available != next_sequence - 1 || xe_pause_gate_is_paused(pause_gate_.get()) ||
        retro_atomic_load_acquire_int(&cancelled_) || retro_atomic_load_acquire_int(&shutdown_)) {
      return available;
    }
    auto wait = timeout - clock::now();
    if (wait <= clock::duration::zero()) {
      return available;
    }
    // A negative due time is relative, in 100 ns units.
    LARGE_INTEGER due_time;
    due_time.QuadPart =
        -std::chrono::ceil<
             std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(wait)
             .count();
    HANDLE handles[] = {wake_event_, wait_timer_};
    DWORD handle_count = 1;
    if (wait_timer_ &&
        SetWaitableTimer(wait_timer_, &due_time, 0, nullptr, nullptr, false)) {
      handle_count = 2;
    }
    WaitForMultipleObjects(
        handle_count, handles, false,
        handle_count == 2
            ? INFINITE
            : DWORD(
                  std::chrono::ceil<std::chrono::milliseconds>(wait).count()));
    // A publish from another thread sets the event once it is visible. This
    // thread's own publishes came before the check above.
    return claim_strategy_.last_published_after(next_sequence - 1);
#else
    for (;;) {
      const int key = retro_eventcount_prepare_wait(&changed_);
      dp::sequence_t available = claim_strategy_.last_published_after(next_sequence - 1);
      const auto remaining = std::chrono::ceil<std::chrono::microseconds>(timeout - clock::now()).count();
      if (available != next_sequence - 1 || remaining <= 0 ||
          xe_pause_gate_is_paused(pause_gate_.get()) ||
          retro_atomic_load_acquire_int(&cancelled_) || retro_atomic_load_acquire_int(&shutdown_)) {
        retro_eventcount_cancel_wait(&changed_);
        return available;
      }
      retro_eventcount_commit_wait_timeout(&changed_, key, remaining);
    }
#endif
  }

  static constexpr clock::duration kIdleWait = std::chrono::seconds(60);

  // This ring buffer will be used to introduce timers queued by the public API
  static constexpr size_t kWaitCount = 512;
  dp::ring_buffer<std::shared_ptr<WaitItem>> buffer_;

  WaitStrat wait_strategy_;
  dp::multi_threaded_claim_strategy<WaitStrat> claim_strategy_;
  dp::sequence_barrier<WaitStrat> consumed_;

  // This is a _sorted_ (ascending due_) list of active timers managed by a
  // dedicated thread
  std::forward_list<std::shared_ptr<WaitItem>> wait_queue_;
  retro_atomic_int_t shutdown_, cancelled_ = 0;
  sthread_t* dispatch_thread_ = nullptr;
  retro_eventcount_t changed_;
  std::unique_ptr<xe_pause_gate, decltype(&xe_pause_gate_free)> pause_gate_{nullptr, xe_pause_gate_free};
#if XE_PLATFORM_WIN32
  // Set after a publish from another thread.
  HANDLE wake_event_ = nullptr;
  HANDLE wait_timer_ = nullptr;
#endif
};

// No worker may start under the Windows DLL loader lock.
static std::mutex timer_queue_mutex_;
static std::unique_ptr<TimerQueue> timer_queue_;
static retro_atomic_ptr_t published_timer_queue_ = nullptr;

static TimerQueue& GetTimerQueue() {
  if (auto* ready = static_cast<TimerQueue*>(retro_atomic_load_acquire_ptr(&published_timer_queue_)))
    return *ready;
  std::lock_guard<std::mutex> lock(timer_queue_mutex_);
  if (!timer_queue_) timer_queue_ = std::make_unique<TimerQueue>();
  retro_atomic_store_release_ptr(&published_timer_queue_, timer_queue_.get());
  return *timer_queue_;
}

void ShutdownTimerQueue() {
  std::unique_ptr<TimerQueue> stopped;
  {
    std::lock_guard<std::mutex> lock(timer_queue_mutex_);
    retro_atomic_store_release_ptr(&published_timer_queue_, nullptr);
    stopped = std::move(timer_queue_);
  }
  // Destruction uses the queue's existing wake-and-join sequence outside the
  // manager lock, so an in-flight callback can finish.
}

bool PauseTimerQueueForState(int64_t timeout_us) {
  auto* queue = static_cast<TimerQueue*>(retro_atomic_load_acquire_ptr(&published_timer_queue_));
  return !queue || queue->PauseForState(timeout_us);
}
void ResumeTimerQueueFromState() {
  auto* queue = static_cast<TimerQueue*>(retro_atomic_load_acquire_ptr(&published_timer_queue_));
  if (queue) queue->ResumeFromState();
}

void TimerQueueWaitItem::Disarm() {
  int state;
  if (retro_atomic_load_acquire_int(&state_) == static_cast<int>(State::kDisarmed)) return;
  if (parent_queue_->IsDispatchThread()) {
    state = retro_atomic_load_acquire_int(&state_);
    if (state == static_cast<int>(State::kInCallbackSelfDisarmed)) return;
    if (retro_atomic_cas_int(&state_, static_cast<int>(State::kInCallback),
                              static_cast<int>(State::kInCallbackSelfDisarmed))) return;
  }
  for (;;) {
    state = retro_atomic_load_acquire_int(&state_);
    if (state == static_cast<int>(State::kDisarmed)) return;
    if (state == static_cast<int>(State::kIdle) &&
        retro_atomic_cas_int(&state_, state, static_cast<int>(State::kDisarmed))) {
      parent_queue_->Cancelled();
      return;
    }
    // No sleep/backoff polling: the completion publication wakes waiters.
    parent_queue_->WaitForCallback(this);
  }
}
// unused
std::weak_ptr<WaitItem> QueueTimerOnce(std::function<void(void*)> callback,
                                       void* userdata,
                                       WaitItem::clock::time_point due) {
  TimerQueue& queue = GetTimerQueue();
  return queue.QueueTimer(
      std::make_shared<WaitItem>(std::move(callback), userdata, &queue,
                                 due, WaitItem::clock::duration::zero()));
}
// only used by HighResolutionTimer
std::weak_ptr<WaitItem> QueueTimerRecurring(
    std::function<void(void*)> callback, void* userdata,
    WaitItem::clock::time_point due, WaitItem::clock::duration interval) {
  TimerQueue& queue = GetTimerQueue();
  return queue.QueueTimer(std::make_shared<WaitItem>(
      std::move(callback), userdata, &queue, due, interval));
}

}  // namespace threading
}  // namespace xe
