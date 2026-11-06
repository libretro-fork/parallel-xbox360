/* C89: concurrent request tickets, unsigned wrap and flag publication. */
#include "libretro/core_launch_state.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "launch state line %u\n", (unsigned)__LINE__); abort(); } } while (0)
enum { PRODUCERS = 4, REQUESTS = 50000, INTERVALS = 50000 };
typedef struct test_state {
  xe_launch_state state;
  retro_atomic_int_t done;
  retro_atomic_int_t *seen;
  unsigned payload;
} test_state;

static void requests(void *opaque) {
  test_state *test = (test_state*)opaque;
  uint64_t ticket;
  unsigned i;
  for (i = 0; i < REQUESTS; ++i) {
    ticket = xe_launch_state_request(&test->state);
    CHECK(ticket > 0 && ticket <= PRODUCERS * REQUESTS);
    CHECK(retro_atomic_cas_int(&test->seen[(size_t)ticket - 1], 0, 1));
  }
  retro_atomic_fetch_add_int(&test->done, 1);
}
static void publish(void *opaque) {
  test_state *test = (test_state*)opaque;
  unsigned i;
  for (i = 1; i <= INTERVALS; ++i) {
    while (xe_launch_state_active(&test->state)) {}
    test->payload = i;
    xe_launch_state_set_active(&test->state, 1);
  }
}
int main(void) {
  test_state test;
  sthread_t *threads[PRODUCERS], *publisher;
  uint64_t latest, previous;
  unsigned i;
  xe_launch_state_init(&test.state);
  CHECK(!xe_launch_state_active(&test.state));
  CHECK(xe_launch_state_latest(&test.state) == 0);
  CHECK(xe_launch_state_request(&test.state) == 1);
  CHECK(xe_launch_state_request(&test.state) == 2);
  CHECK(xe_launch_state_latest(&test.state) == 2);
  retro_atomic_store_relaxed_64(&test.state.generation, INT64_MAX);
  CHECK(xe_launch_state_request(&test.state) == (UINT64_C(1) << 63));
  CHECK(xe_launch_state_latest(&test.state) == (UINT64_C(1) << 63));
  retro_atomic_store_relaxed_64(&test.state.generation, -1);
  CHECK(xe_launch_state_request(&test.state) == 0);
  xe_launch_state_set_active(&test.state, -7);
  CHECK(xe_launch_state_active(&test.state));
  xe_launch_state_set_active(&test.state, 0);
  CHECK(!xe_launch_state_active(&test.state));

  xe_launch_state_init(&test.state);
  retro_atomic_store_relaxed_int(&test.done, 0);
  test.seen = (retro_atomic_int_t*)calloc(PRODUCERS * REQUESTS, sizeof(*test.seen));
  CHECK(test.seen);
  for (i = 0; i < PRODUCERS * REQUESTS; ++i)
    retro_atomic_store_relaxed_int(&test.seen[i], 0);
  for (i = 0; i < PRODUCERS; ++i) {
    threads[i] = sthread_create(requests, &test);
    CHECK(threads[i]);
  }
  previous = 0;
  do {
    latest = xe_launch_state_latest(&test.state);
    CHECK(latest >= previous && latest <= PRODUCERS * REQUESTS);
    previous = latest;
  } while (retro_atomic_load_acquire_int(&test.done) != PRODUCERS);
  for (i = 0; i < PRODUCERS; ++i) sthread_join(threads[i]);
  CHECK(xe_launch_state_latest(&test.state) == PRODUCERS * REQUESTS);
  for (i = 0; i < PRODUCERS * REQUESTS; ++i)
    CHECK(retro_atomic_load_acquire_int(&test.seen[i]) == 1);
  free((void*)test.seen);

  xe_launch_state_init(&test.state);
  test.payload = 0;
  publisher = sthread_create(publish, &test);
  CHECK(publisher);
  for (i = 1; i <= INTERVALS; ++i) {
    while (!xe_launch_state_active(&test.state)) {}
    CHECK(test.payload == i);
    xe_launch_state_set_active(&test.state, 0);
  }
  sthread_join(publisher);
  puts("PASS: 200000 unique concurrent launch tickets, unsigned wrap, 50000 published intervals");
  return 0;
}
