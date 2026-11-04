#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "core_events.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %u\n",(unsigned)__LINE__); abort(); } } while (0)
#define PRODUCERS 6
#define COUNT 30000
static retro_atomic_int_t ready, start, finished;
static unsigned ids[PRODUCERS];
static void producer(void *opaque) {
   unsigned id = *(unsigned *)opaque, i;
   char prefix[8], text[64];
   sprintf(prefix, "P%u", id);
   retro_atomic_fetch_add_int(&ready, 1);
   while (!retro_atomic_load_acquire_int(&start)) {}
   for (i = 1; i <= COUNT; ++i) {
      sprintf(text, "%u:%u", i, i ^ (id * 1234567u));
      xe_libretro_notice_post(prefix, text);
   }
   retro_atomic_fetch_add_int(&finished, 1);
}
int main(void) {
   xe_notice_view views[8], view;
   sthread_t *threads[PRODUCERS];
   unsigned i, id, value, checksum, received = 0, last[PRODUCERS] = {0};
   char text[1100], expected[16];
   xe_libretro_clear_title_stop();
   CHECK(!xe_libretro_take_title_stop());
   xe_libretro_request_title_stop(); xe_libretro_request_title_stop();
   CHECK(xe_libretro_take_title_stop() && !xe_libretro_take_title_stop());
   xe_libretro_request_title_stop(); xe_libretro_clear_title_stop();
   CHECK(!xe_libretro_take_title_stop());
   xe_libretro_notice_clear();
   CHECK(!xe_libretro_notice_post(NULL, NULL) && !xe_libretro_notice_acquire(&view));
   CHECK(xe_libretro_notice_post("title", "answer"));
   CHECK(xe_libretro_notice_acquire(&view) && !strcmp(view.text, "title: answer"));
   for (i = 0; i < 100; ++i) CHECK(xe_libretro_notice_post(NULL, "new"));
   CHECK(!strcmp(view.text, "title: answer"));
   CHECK(xe_libretro_notice_acquire(&views[0]) && !strcmp(views[0].text, "new"));
   xe_libretro_notice_release(&views[0]); xe_libretro_notice_release(&view);
   xe_libretro_notice_release(&view); xe_libretro_notice_release(NULL);
   for (i = 0; i < 8; ++i) {
      sprintf(text, "held%u", i);
      CHECK(xe_libretro_notice_post(NULL, text) && xe_libretro_notice_acquire(&views[i]));
   }
   CHECK(!xe_libretro_notice_post(NULL, "full"));
   for (i = 0; i < 8; ++i) {
      sprintf(expected, "held%u", i); CHECK(!strcmp(views[i].text, expected));
      xe_libretro_notice_release(&views[i]);
   }
   memset(text, 'a', 1022); memcpy(text + 1022, "\xe2\x82\xac", 4);
   CHECK(xe_libretro_notice_post(NULL, text) && xe_libretro_notice_acquire(&view));
   CHECK(strlen(view.text) == 1022); xe_libretro_notice_release(&view);
   memset(text, 'a', 1020); memcpy(text + 1020, "\xe2\x82\xac", 4);
   CHECK(xe_libretro_notice_post(NULL, text) && xe_libretro_notice_acquire(&view));
   CHECK(strlen(view.text) == 1023 && !memcmp(view.text + 1020, "\xe2\x82\xac", 3));
   xe_libretro_notice_release(&view);
   memset(text, 'b', 1099); text[1099] = 0;
   CHECK(xe_libretro_notice_post(NULL, text) && xe_libretro_notice_acquire(&view));
   CHECK(strlen(view.text) == 1023); xe_libretro_notice_release(&view);
   CHECK(xe_libretro_notice_post(NULL, "old")); xe_libretro_notice_clear();
   CHECK(!xe_libretro_notice_acquire(&view));
   for (i = 0; i < PRODUCERS; ++i) {
      ids[i] = i; threads[i] = sthread_create(producer, &ids[i]); CHECK(threads[i]);
   }
   while (retro_atomic_load_acquire_int(&ready) != PRODUCERS) {}
   retro_atomic_store_release_int(&start, 1);
   for (;;) {
      if (xe_libretro_notice_acquire(&view)) {
         CHECK(view.text[0] == 'P' && view.text[1] >= '0' && view.text[1] < '0' + PRODUCERS && view.text[2] == ':' && view.text[3] == ' ');
         id = (unsigned)(view.text[1] - '0');
         CHECK(sscanf(view.text + 4, "%u:%u", &value, &checksum) == 2);
         CHECK(value > last[id] && value <= COUNT && checksum == (value ^ (id * 1234567u)));
         last[id] = value; ++received;
         xe_libretro_notice_release(&view);
      } else if (retro_atomic_load_acquire_int(&finished) == PRODUCERS) break;
   }
   for (i = 0; i < PRODUCERS; ++i) sthread_join(threads[i]);
   CHECK(received);
   xe_libretro_notice_clear();
   puts("PASS: borrowed notice leases, saturation, UTF-8 bounds, coalescing and six concurrent producers");
   return 0;
}
