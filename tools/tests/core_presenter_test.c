/* C89: frontend capture, failed refresh, blanking and concurrent ownership. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>

void *xe_test_presenter_new(void);
void xe_test_presenter_free(void *p);
int xe_test_presenter_refresh(void *p, unsigned value, int active, int fail);
int xe_test_presenter_capture(void *p, unsigned *pixels);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %u\n", (unsigned)__LINE__); exit(1); } } while (0)
#define FRAMES 200000
struct state { void *presenter; retro_atomic_int_t done; };

void xe_test_presenter_fill(unsigned *pixels, unsigned value)
{
   unsigned i;
   for (i = 0; i < 256; ++i) pixels[i] = value;
}

static void produce(void *opaque)
{
   struct state *state = (struct state*)opaque;
   unsigned i;
   for (i = 1; i <= FRAMES; ++i)
      CHECK(xe_test_presenter_refresh(state->presenter, i, 1, 0));
   retro_atomic_store_release_int(&state->done, 1);
}

int main(void)
{
   struct state state;
   sthread_t *producer;
   unsigned pixels[256], last, i, captures;
   state.presenter = xe_test_presenter_new();
   CHECK(state.presenter);
   retro_atomic_store_relaxed_int(&state.done, 0);
   CHECK(!xe_test_presenter_capture(state.presenter, pixels));
   CHECK(xe_test_presenter_refresh(state.presenter, 7, 1, 0));
   CHECK(!xe_test_presenter_refresh(state.presenter, 8, 1, 1));
   CHECK(xe_test_presenter_capture(state.presenter, pixels));
   CHECK(pixels[0] == 7);
   CHECK(!xe_test_presenter_refresh(state.presenter, 0, 0, 0));
   CHECK(!xe_test_presenter_capture(state.presenter, pixels));
   CHECK(!xe_test_presenter_refresh(state.presenter, 0, 0, 0));
   CHECK(xe_test_presenter_refresh(state.presenter, 0, 1, 0));
   last = captures = 0;
   producer = sthread_create(produce, &state);
   CHECK(producer);
   do {
      CHECK(xe_test_presenter_capture(state.presenter, pixels));
      CHECK(pixels[0] >= last);
      for (i = 1; i < 256; ++i) CHECK(pixels[i] == pixels[0]);
      last = pixels[0];
      ++captures;
   } while (!retro_atomic_load_acquire_int(&state.done));
   sthread_join(producer);
   CHECK(xe_test_presenter_capture(state.presenter, pixels));
   for (i = 0; i < 256; ++i) CHECK(pixels[i] == FRAMES);
   xe_test_presenter_free(state.presenter);
   printf("PASS: frontend mailbox, failed refresh, blanking, %u captures/%u frames\n",
          captures, (unsigned)FRAMES);
   return 0;
}
