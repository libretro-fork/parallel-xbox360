/* CPU-only snapshot/reference fixture. Uploads are byte-array test doubles. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "core_snapshot_pair.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n", __LINE__, #x); result = 1; goto done; } } while (0)
static void generate(unsigned char *out, size_t size, uint32_t value) {
  size_t i;
  memset(out, 0, size);
  for (i = 0; i < size; i += 13) out[i] = (unsigned char)(value + i);
  out[0] = (unsigned char)value;
  out[size - 1] = (unsigned char)(value >> 8);
}
int xe_snapshot_pair_test(size_t size) {
  xe_snapshot_pair pair;
  unsigned char *slots = NULL, *reference = NULL, *shadow = NULL, *old_upload = NULL, *new_upload = NULL;
  int result = 0, valid = 0, old_current = 0, new_current = 0;
  uint32_t draw, random = 0x891a32, value = 0, last = 0;
  size_t old_calls = 0, new_calls = 0, failed = 0, committed = 0, unchanged = 0;
  CHECK(size > 0 && size <= 16384);
  slots = (unsigned char *)malloc(size * 2); reference = (unsigned char *)malloc(size);
  shadow = (unsigned char *)malloc(size); old_upload = (unsigned char *)malloc(size); new_upload = (unsigned char *)malloc(size);
  CHECK(slots && reference && shadow && old_upload && new_upload);
  xe_snapshot_pair_init(&pair, slots, slots + size);
  memset(slots, 0xa5, size * 2); memset(shadow, 0xdb, size);
  memset(old_upload, 0x3c, size); memset(new_upload, 0x3c, size);
  for (draw = 0; draw < 8192; ++draw) {
    int expected, changed, fail;
    void *active = pair.active;
    unsigned char *candidate = (unsigned char *)pair.candidate;
    random = random * UINT32_C(1664525) + UINT32_C(1013904223);
    if (draw % 7 == 0) { old_current = 0; new_current = 0; }
    if (draw % 1024 == 0) {
      xe_snapshot_pair_init(&pair, slots, slots + size);
      valid = old_current = new_current = 0;
      memset(slots, 0xa5, size * 2);
      active = pair.active; candidate = (unsigned char *)pair.candidate;
    }
    if (draw % 4 == 0) value = random;
    if (draw % 4 == 1) value = last; /* Retry identical data after failed request. */
    if (draw % 4 == 2) value ^= UINT32_C(0x100); /* Tail byte changes. */
    generate(reference, size, value); generate(candidate, size, value);
    expected = !valid || memcmp(shadow, reference, size) != 0;
    if (valid) CHECK(memcmp(active, shadow, size) == 0);
    changed = xe_snapshot_pair_commit(&pair, size);
    CHECK(changed == expected && pair.valid && pair.active == (changed ? (void *)candidate : active));
    CHECK(memcmp(pair.active, reference, size) == 0);
    if (expected) { memcpy(shadow, reference, size); valid = 1; old_current = 0; ++committed; }
    else ++unchanged;
    if (changed) new_current = 0;
    CHECK(old_current == new_current);
    fail = draw % 4 == 0;
    if (!old_current) {
      ++old_calls;
      if (!fail) { memcpy(old_upload, reference, size); old_current = 1; }
      else ++failed;
    }
    if (!new_current) {
      ++new_calls;
      if (!fail) { memcpy(new_upload, pair.active, size); new_current = 1; }
    }
    CHECK(old_current == new_current && old_calls == new_calls);
    CHECK(memcmp(old_upload, new_upload, size) == 0);
    last = value;
  }
  CHECK(failed && committed && unchanged);
done:
  free(slots); free(reference); free(shadow); free(old_upload); free(new_upload);
  return result;
}
#ifndef XE_SNAPSHOT_BRIDGE
int main(void) {
  const size_t sizes[] = {1, 7, 8, 16, 31, 64, 127, 256, 512, 688, 1024, 4096};
  size_t i;
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) if (xe_snapshot_pair_test(sizes[i])) return 1;
  puts("Snapshot pair C89: 98,304 exact vector-shadow reference decisions/uploads, poisoned first use, changed tails, frame invalidation, failed upload retry and resets pass");
  return 0;
}
#endif
