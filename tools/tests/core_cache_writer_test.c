/* C89: bounded FIFO wrap, borrowed record lifetime and shutdown draining. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_cache_writer.h"
#include <stdio.h>
#include <rthreads/rthreads.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %u\n",(unsigned)__LINE__); exit(1); } } while (0)
struct state { unsigned next, barriers; uintptr_t producer; };
struct record { unsigned index; unsigned char payload[37]; };
static void consume(void *opaque, unsigned kind, const void *data, size_t size)
{
   struct state *state = (struct state *)opaque;
   struct record record;
   unsigned i;
   CHECK(sthread_get_current_thread_id() != state->producer);
   if (kind == 2) {
      CHECK(size == 0);
      CHECK(state->next % 97 == 0);
      ++state->barriers;
      return;
   }
   CHECK(kind == 1 && size == sizeof(record));
   memcpy(&record, data, sizeof(record));
   CHECK(record.index == state->next++);
   for (i = 0; i < sizeof(record.payload); ++i)
      CHECK(record.payload[i] == (unsigned char)(record.index + i));
   /* Keep reading the borrowed slot while the producer advances. */
   CHECK(memcmp(&record, data, sizeof(record)) == 0);
}
int main(void)
{
   struct state state;
   struct record record;
   xe_cache_writer *writer;
   unsigned round, i, j, count;
   size_t split;
   CHECK(!xe_cache_writer_new((size_t)-1, 1, consume, NULL));
   CHECK(!xe_cache_writer_new(8, 0, consume, NULL));
   CHECK(!xe_cache_writer_new(8, (size_t)-1, consume, NULL));
   CHECK(!xe_cache_writer_new(8, 1, NULL, NULL));
   CHECK(!xe_cache_writer_submit(NULL, 1, NULL, 0));
   xe_cache_writer_free(NULL);
   for (round = 0; round < 90; ++round) {
      memset(&state, 0, sizeof(state));
      state.producer = sthread_get_current_thread_id();
      writer = xe_cache_writer_new(sizeof(record), (size_t)1 << (round % 6), consume, &state);
      CHECK(writer);
      CHECK(!xe_cache_writer_submit(writer, 1, &record, sizeof(record)+1));
      CHECK(!xe_cache_writer_submit(writer, 1, NULL, 1));
      CHECK(!xe_cache_writer_submit_parts(writer, 1, &record,
            sizeof(record), &record, (size_t)-1));
      CHECK(!xe_cache_writer_submit_parts(writer, 1, &record, 1, NULL, 1));
      count = 3000 + round;
      for (i = 0; i < count; ++i) {
         memset(&record, 0, sizeof(record));
         record.index = i;
         for (j = 0; j < sizeof(record.payload); ++j)
            record.payload[j] = (unsigned char)(i + j);
         split = i % (sizeof(record) + 1);
         CHECK(xe_cache_writer_submit_parts(writer, 1, &record, split,
               (const unsigned char *)&record + split, sizeof(record) - split));
         memset(&record, 0xcc, sizeof(record));
         if ((i+1) % 97 == 0)
            CHECK(xe_cache_writer_submit(writer, 2, NULL, 0));
      }
      /* No completion polling: destruction must consume every published row. */
      xe_cache_writer_free(writer);
      CHECK(state.next == count && state.barriers == count / 97);
   }
   for (round = 0; round < 90; ++round) {
      writer = xe_cache_writer_new(sizeof(record), 1, consume, &state);
      CHECK(writer);
      xe_cache_writer_free(writer);
   }
   puts("PASS bounded cache writer FIFO, backpressure, barriers and shutdown drain");
   return 0;
}
