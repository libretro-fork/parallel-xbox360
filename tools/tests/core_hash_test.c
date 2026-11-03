/* Cache-key compatibility regression; MSVC C89.
 * Golden fold values generated from the former xxHash 0.8 implementation.
 * Fold every tested result so a test does not merely compare two new paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <encodings/xxh3.h>
#include <encodings/xxh64.h>
static const uint64_t expected3[3] = {
 UINT64_C(0x34197f5625080849), UINT64_C(0x6651fe2935387942), UINT64_C(0x3f063bffcb60a1d2)
};
static const uint64_t expected64[3] = {
 UINT64_C(0x017914c0be231f12), UINT64_C(0xad589b5c050e23a2), UINT64_C(0x15dafacd788a0d24)
};
int main(void) {
  unsigned char *p;
  size_t n, offset, part, step;
  unsigned seed_index;
  uint64_t seed, a, b, fold3, fold64;
  xxh3_state_t s3;
  xxh64_state_t s64;
  p = (unsigned char *)malloc(1048608); if (!p) return 2;
  for (n = 0; n < 1048608; ++n)
    p[n] = (unsigned char)((n * 131u) ^ (n >> 7) ^ (n >> 13));
  for (seed_index = 0; seed_index < 3; ++seed_index) {
    seed = seed_index == 0 ? 0 : seed_index == 1 ? UINT64_C(0x123456789abcdef0) : UINT64_MAX;
    fold3 = fold64 = 0;
    for (n = 0; n <= 1048576; n += n < 4096 ? 1 : 997) {
      offset = n % 17;
      a = xxh3_64(p + offset, n, seed); b = xxh64(p + offset, n, seed);
      fold3 = (fold3 ^ a) * UINT64_C(0x9e3779b185ebca87);
      fold64 = (fold64 ^ b) * UINT64_C(0x9e3779b185ebca87);
      xxh3_reset(&s3, seed); xxh64_reset(&s64, seed);
      xxh3_update(&s3, p, 0); xxh64_update(&s64, p, 0);
      for (part = 0; part < n; part += step) {
        step = ((part * 17 + 13) % 1103) + 1;
        if (step > n - part) step = n - part;
        xxh3_update(&s3, p + offset + part, step);
        xxh64_update(&s64, p + offset + part, step);
        /* A digest must leave the state usable for subsequent updates. */
        if (part % 17 == 0 &&
            (xxh3_digest(&s3) != xxh3_64(p + offset, part + step, seed) ||
             xxh64_digest(&s64) != xxh64(p + offset, part + step, seed))) return 3;
      }
      if (xxh3_digest(&s3) != a || xxh64_digest(&s64) != b) return 4;
    }
    if (fold3 != expected3[seed_index] || fold64 != expected64[seed_index]) {
      fprintf(stderr, "Legacy hash compatibility failed, seed %u\n", seed_index); return 5;
    }
  }
  free(p);
  puts("PASS: legacy XXH3/XXH64 golden folds, three seeds, unaligned boundaries and streaming");
  return 0;
}
