#include <stdio.h>
#include <stdlib.h>
#include "core_resolve_recent.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"recent C89 line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static xe_resolve_recent state;
static uint64_t colliding[4096];
static uint32_t random_word = 0x193a572bu;
static uint32_t next_word(void) {
  random_word ^= random_word << 13; random_word ^= random_word >> 17;
  random_word ^= random_word << 5; return random_word;
}
static void validate(void) {
  uint32_t i, count = 0;
  for (i = 0; i < XE_RESOLVE_RECENT_BUCKETS; ++i) if (xe_resolve_recent_live(&state, i)) {
    CHECK(xe_resolve_recent_slot(&state, state.keys[i]) == i); ++count;
  }
  CHECK(count == state.count); CHECK(count <= XE_RESOLVE_RECENT_CAPACITY);
  count = 0;
  for (i = xe_resolve_recent_next(&state, 0); i < XE_RESOLVE_RECENT_BUCKETS; i = xe_resolve_recent_next(&state, i + 1)) {
    CHECK(xe_resolve_recent_live(&state, i)); ++count;
  }
  CHECK(count == state.count);
  CHECK(xe_resolve_recent_next(&state, XE_RESOLVE_RECENT_BUCKETS) == XE_RESOLVE_RECENT_BUCKETS);
}
int main(void) {
  uint64_t key;
  uint32_t i, count = 0, at;
  memset(&state, 0xa7, sizeof(state)); xe_resolve_recent_reset(&state);
  CHECK(!xe_resolve_recent_read(&state, 0, 0, 0));
  CHECK(xe_resolve_recent_read(&state, 0, UINT32_MAX - 10, 1));
  CHECK(xe_resolve_recent_read(&state, 0, 49, 0));
  CHECK(!xe_resolve_recent_read(&state, 0, 50, 0));
  CHECK(state.count == 1);
  CHECK(xe_resolve_recent_read(&state, UINT64_MAX, 4, 1)); validate();
  xe_resolve_recent_reset(&state);
  /* Wrap the longest permitted cluster across the bucket-array boundary. */
  for (key = 0; count < 4096; ++key)
    if (xe_resolve_recent_home(key) == XE_RESOLVE_RECENT_BUCKETS - 3) colliding[count++] = key;
  for (i = 0; i < 4096; ++i) CHECK(xe_resolve_recent_read(&state, colliding[i], i & 1 ? 61 : 0, 1));
  validate();
  CHECK(!xe_resolve_recent_read(&state, colliding[0], 61, 0));
  CHECK(state.count == 4096); /* Queries must not prune even a full table. */
  CHECK(xe_resolve_recent_read(&state, colliding[1], 61, 1));
  CHECK(state.count == 2048); validate();
  for (i = 0; i < 4096; ++i)
    CHECK(xe_resolve_recent_read(&state, colliding[i], 61, 0) == (int)(i & 1));
  for (i = 0; i < 4096; i += 2) xe_resolve_recent_read(&state, colliding[i], 61, 1);
  CHECK(state.count == 4096); validate();
  /* A full all-recent map clears before updating even an existing key. */
  xe_resolve_recent_read(&state, colliding[123], 121, 1);
  CHECK(state.count == 1); validate();
  CHECK(!xe_resolve_recent_read(&state, colliding[122], 121, 0));
  xe_resolve_recent_reset(&state);
  for (i = 0; i < 4096; ++i) xe_resolve_recent_read(&state, colliding[i], UINT32_MAX - 30, 1);
  xe_resolve_recent_read(&state, UINT64_MAX, 30, 1);
  CHECK(state.count == 1); validate(); /* Age 61 across frame wrap. */
  for (i = 0; i < 100000; ++i) {
    key = next_word() % 6000;
    xe_resolve_recent_read(&state, key, i / 43, 1);
    if (!(i % 211)) validate();
  }
  validate();
  at = state.count; xe_resolve_recent_reset(&state); CHECK(!state.count);
  CHECK(!xe_resolve_recent_read(&state, key, 0, 0));
  printf("Recent C89: 4096-key wrapping collisions, expiry/backshift, cap-clear, query purity, frame wrap and 100000 reuse events pass; storage=%lu bytes, prior live=%u\n", (unsigned long)sizeof(state), at);
  return 0;
}
