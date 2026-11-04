#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_pipeline_order.h"
#include "libretro/core_cache_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
struct result { size_t ticket; unsigned check; unsigned char bytes[79]; };
struct state { xe_pipeline_order *order; size_t expected, published, discarded; };
static void publish(void *opaque, void *payload)
{
   struct state *state = (struct state *)opaque;
   struct result *result = (struct result *)payload;
   unsigned i;
   CHECK(result->ticket == state->expected++);
   CHECK(result->check == (unsigned)result->ticket * 37);
   for (i = 0; i < sizeof(result->bytes); ++i)
      CHECK(result->bytes[i] == (unsigned char)(result->ticket + i));
   ++state->published;
}
static void discard(void *opaque, void *payload)
{
   struct state *state = (struct state *)opaque;
   struct result *result = (struct result *)payload;
   CHECK(result->ticket > 1 && result->ticket < 4);
   ++state->discarded;
}
static void finish(void *opaque, unsigned kind, const void *data, size_t size)
{
   struct state *state = (struct state *)opaque;
   struct result *result;
   size_t ticket;
   unsigned i;
   CHECK(kind == 1 && size == sizeof(ticket));
   memcpy(&ticket, data, sizeof(ticket));
   result = (struct result *)xe_pipeline_order_payload(state->order, ticket);
   CHECK(result && result->ticket == ticket);
   result->check = (unsigned)ticket * 37;
   for (i = 0; i < sizeof(result->bytes); ++i)
      result->bytes[i] = (unsigned char)(ticket + i);
   CHECK(xe_pipeline_order_complete(state->order, ticket));
}
int main(void)
{
   struct state state;
   xe_cache_writer *workers[6];
   struct result *result;
   size_t tickets[4], ticket;
   unsigned round, i;
   CHECK(!xe_pipeline_order_new(0, 4, publish, NULL));
   CHECK(!xe_pipeline_order_new(3, 4, publish, NULL));
   CHECK(!xe_pipeline_order_new(4, (size_t)-1, publish, NULL));
   CHECK(!xe_pipeline_order_new(4, 4, NULL, NULL));
   CHECK(!xe_pipeline_order_reserve(NULL));
   CHECK(!xe_pipeline_order_complete(NULL, 1));
   xe_pipeline_order_reset(NULL, NULL, NULL);
   xe_pipeline_order_free(NULL);
   for (round = 0; round < 8; ++round) {
      memset(&state, 0, sizeof(state)); state.expected = 1;
      state.order = xe_pipeline_order_new((size_t)1 << round,
            sizeof(struct result), publish, &state);
      CHECK(state.order);
      CHECK(!xe_pipeline_order_payload(state.order, 0));
      CHECK(!xe_pipeline_order_payload(state.order, 1));
      CHECK(!xe_pipeline_order_complete(state.order, 1));
      for (i = 0; i < 6; ++i) {
         workers[i] = xe_cache_writer_new(sizeof(size_t), 16, finish, &state);
         CHECK(workers[i]);
      }
      for (i = 0; i < 12000; ++i) {
         ticket = xe_pipeline_order_reserve(state.order);
         CHECK(ticket == (size_t)i + 1);
         result = (struct result *)xe_pipeline_order_payload(state.order, ticket);
         CHECK(result); result->ticket = ticket;
         CHECK(xe_cache_writer_submit(workers[i % 6], 1, &ticket, sizeof(ticket)));
      }
      for (i = 0; i < 6; ++i) xe_cache_writer_free(workers[i]);
      CHECK(state.published == 12000);
      CHECK(!xe_pipeline_order_complete(state.order, 12000));
      xe_pipeline_order_reset(state.order, discard, &state);
      CHECK(state.discarded == 0 && xe_pipeline_order_reserve(state.order) == 1);
      xe_pipeline_order_free(state.order);
   }
   memset(&state, 0, sizeof(state)); state.expected = 1;
   state.order = xe_pipeline_order_new(4, sizeof(struct result), publish, &state);
   CHECK(state.order);
   for (i = 0; i < 4; ++i) {
      tickets[i] = xe_pipeline_order_reserve(state.order);
      result = (struct result *)xe_pipeline_order_payload(state.order, tickets[i]);
      result->ticket = tickets[i];
   }
   finish(&state, 1, &tickets[2], sizeof(size_t));
   finish(&state, 1, &tickets[1], sizeof(size_t));
   CHECK(state.published == 0);
   xe_pipeline_order_reset(state.order, discard, &state);
   CHECK(state.discarded == 2);
   CHECK(xe_pipeline_order_reserve(state.order) == 1);
   xe_pipeline_order_free(state.order);
   puts("PASS: reusable pipeline publication, six finishers, ordered output, wrap, missing predecessor and reset");
   return 0;
}
