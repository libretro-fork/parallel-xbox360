/* Exercise the real host timer dispatcher, including retained-item shutdown. */
#include "xenia/base/threading_timer_queue.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)

struct test_state {
  retro_atomic_int_t calls = 0;
  retro_eventcount_t changed;
  std::shared_ptr<xe::threading::TimerQueueWaitItem> self;
};

static bool await_call(test_state *state, int wanted) {
  int64_t deadline = cpu_features_get_time_usec() + 2000000;
  while (retro_atomic_load_acquire_int(&state->calls) != wanted) {
    int key = retro_eventcount_prepare_wait(&state->changed);
    int64_t remaining = deadline - cpu_features_get_time_usec();
    if (remaining <= 0) {
      retro_eventcount_cancel_wait(&state->changed);
      return false;
    }
    if (retro_atomic_load_acquire_int(&state->calls) != wanted)
      retro_eventcount_commit_wait_timeout(&state->changed, key, remaining);
    else retro_eventcount_cancel_wait(&state->changed);
  }
  return true;
}

int main() {
  using namespace xe::threading;
  using clock = TimerQueueWaitItem::clock;
  test_state state;
  CHECK(retro_eventcount_init(&state.changed));
  auto retained = QueueTimerOnce([](void *) { CHECK(false); }, nullptr,
                                clock::now() + std::chrono::hours(1)).lock();
  CHECK(retained);
  CHECK(PauseTimerQueueForState(2000000));
  for (unsigned i = 0; i < 2000; ++i) {
    state.self = QueueTimerRecurring([](void *context) {
      test_state *value = static_cast<test_state *>(context);
      value->self->Disarm();
      value->self->Disarm();
      retro_atomic_fetch_add_int(&value->calls, 1);
      retro_eventcount_notify(&value->changed);
    }, &state, clock::now(), std::chrono::microseconds(1)).lock();
    CHECK(state.self);
    CHECK(retro_atomic_load_acquire_int(&state.calls) == static_cast<int>(i));
    ResumeTimerQueueFromState();
    CHECK(await_call(&state, static_cast<int>(i + 1)));
    CHECK(PauseTimerQueueForState(2000000));
    state.self->Disarm();
    state.self.reset();
  }
  ShutdownTimerQueue();
  retained->Disarm();
  retained.reset();
  retro_eventcount_free(&state.changed);
  std::puts("PASS: 2000 timer pause/resume generations, self-cancel twice, shutdown paused and retained-item cancellation");
  return 0;
}
