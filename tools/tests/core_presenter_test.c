/* C89: frontend capture, failed refresh, blanking and concurrent ownership. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <retro_atomic.h>
#include "libretro/core_video_mailbox.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>

void *xe_test_presenter_new(void);
void xe_test_presenter_free(void *p);
int xe_test_presenter_refresh(void *p, unsigned value, int active, int fail);
int xe_test_presenter_capture(void *p, unsigned *pixels);
int xe_test_presenter_nested(void *p);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %u\n", (unsigned)__LINE__); exit(1); } } while (0)
#define FRAMES 200000
struct state { void *presenter; retro_atomic_int_t done, start; };
struct consumer { struct state *state; unsigned captures, busy; };

void xe_test_presenter_fill(unsigned *pixels, unsigned value)
{
   unsigned i;
   for (i = 0; i < 256; ++i) pixels[i] = value;
}

static void produce(void *opaque)
{
   struct state *state = (struct state*)opaque;
   unsigned i;
   while (!retro_atomic_load_acquire_int(&state->start)) {}
   for (i = 1; i <= FRAMES; ++i)
      CHECK(xe_test_presenter_refresh(state->presenter, i, 1, 0));
   retro_atomic_store_release_int(&state->done, 1);
}

static void consume(void *opaque)
{
   struct consumer *consumer = (struct consumer*)opaque;
   struct state *state = consumer->state;
   unsigned pixels[256], last = 0, i;
   while (!retro_atomic_load_acquire_int(&state->start)) {}
   do {
      if (!xe_test_presenter_capture(state->presenter, pixels)) {
         ++consumer->busy;
         continue;
      }
      CHECK(pixels[0] >= last);
      for (i = 1; i < 256; ++i) CHECK(pixels[i] == pixels[0]);
      last = pixels[0];
      ++consumer->captures;
   } while (!retro_atomic_load_acquire_int(&state->done));
}

static void mailbox_contract(void)
{
   xe_video_mailbox mailbox;
   uint32_t held, next;
   unsigned i;
   xe_video_mailbox_init(&mailbox);
   CHECK(mailbox.writable == 1);
   xe_video_mailbox_publish(&mailbox);
   held = xe_video_mailbox_read_begin(&mailbox);
   CHECK(held == 1);
   CHECK(xe_video_mailbox_read_begin(&mailbox) == UINT32_MAX);
   for (i = 0; i < 100000; ++i) {
      CHECK(mailbox.writable < 3 && mailbox.writable != held);
      xe_video_mailbox_publish(&mailbox);
      CHECK(mailbox.writable != held);
   }
   xe_video_mailbox_read_end(&mailbox);
   next = xe_video_mailbox_read_begin(&mailbox);
   CHECK(next < 3 && next != held && next != mailbox.writable);
   xe_video_mailbox_read_end(&mailbox);
}

int main(void)
{
   struct state state;
   sthread_t *producer, *readers[2];
   struct consumer consumers[2];
   unsigned pixels[256], i;
   mailbox_contract();
   state.presenter = xe_test_presenter_new();
   CHECK(state.presenter);
   retro_atomic_store_relaxed_int(&state.done, 0);
   retro_atomic_store_relaxed_int(&state.start, 0);
   CHECK(!xe_test_presenter_capture(state.presenter, pixels));
   CHECK(xe_test_presenter_refresh(state.presenter, 7, 1, 0));
   CHECK(!xe_test_presenter_refresh(state.presenter, 8, 1, 1));
   CHECK(xe_test_presenter_capture(state.presenter, pixels));
   CHECK(pixels[0] == 7);
   CHECK(!xe_test_presenter_refresh(state.presenter, 0, 0, 0));
   CHECK(!xe_test_presenter_capture(state.presenter, pixels));
   CHECK(!xe_test_presenter_refresh(state.presenter, 0, 0, 0));
   CHECK(xe_test_presenter_refresh(state.presenter, 0, 1, 0));
   CHECK(xe_test_presenter_nested(state.presenter));
   for (i = 0; i < 2; ++i) {
      consumers[i].state = &state;
      consumers[i].captures = consumers[i].busy = 0;
      readers[i] = sthread_create(consume, &consumers[i]);
      CHECK(readers[i]);
   }
   producer = sthread_create(produce, &state);
   CHECK(producer);
   retro_atomic_store_release_int(&state.start, 1);
   for (i = 0; i < 2; ++i) sthread_join(readers[i]);
   sthread_join(producer);
   CHECK(xe_test_presenter_capture(state.presenter, pixels));
   for (i = 0; i < 256; ++i) CHECK(pixels[i] == FRAMES);
   xe_test_presenter_free(state.presenter);
   printf("PASS: frontend mailbox, failed refresh, blanking, %u captures/%u frames\n",
          consumers[0].captures + consumers[1].captures, (unsigned)FRAMES);
   return 0;
}
