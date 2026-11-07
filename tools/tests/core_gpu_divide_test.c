/* Exact shader sequence reference, full-u32 boundaries and failure paths. C89. */
#include "core_gpu_divide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #x); exit(1); } } while (0)
typedef struct evaluator {
  uint32_t values[32];
  unsigned count, calls, fail_at;
} evaluator;
static uintptr_t evaluate(void *opaque, enum xe_gpu_divide_op op,
    uintptr_t a, uintptr_t b, uintptr_t c) {
  evaluator *e = (evaluator *)opaque;
  uint32_t av, bv, value = 0;
  if (++e->calls == e->fail_at) return 0;
  CHECK(e->count < 32);
  if (op == XE_GPU_DIV_CONST) value = (uint32_t)a;
  else {
    CHECK(a && b && a < e->count && b < e->count);
    av = e->values[a]; bv = e->values[b];
    switch (op) {
      case XE_GPU_DIV_MUL_HIGH: value = (uint32_t)(((uint64_t)av * bv) >> 32); break;
      case XE_GPU_DIV_EQ: value = av == bv; break;
      case XE_GPU_DIV_MUL: value = av * bv; break;
      case XE_GPU_DIV_SUB: value = av - bv; break;
      case XE_GPU_DIV_GE: value = av >= bv; break;
      case XE_GPU_DIV_ADD: value = av + bv; break;
      case XE_GPU_DIV_SELECT:
        CHECK(c && c < e->count); value = av ? bv : e->values[c]; break;
      default: CHECK(0);
    }
  }
  e->values[e->count] = value;
  return e->count++;
}
static unsigned cases;
static void check_pair(uint32_t n, uint32_t d) {
  evaluator e;
  uintptr_t q = 99, r = 99;
  memset(&e, 0, sizeof(e)); e.count = 4;
  e.values[1] = n; e.values[2] = d;
  CHECK(xe_gpu_divide_prepare(d, &e.values[3]));
  CHECK(xe_gpu_divide_emit(evaluate, &e, 1, 2, 3, &q, &r));
  CHECK(e.calls == 12 && q < e.count && r < e.count);
  CHECK(e.values[q] == n / d && e.values[r] == n % d);
  CHECK((uint64_t)e.values[q] * d + e.values[r] == n);
  ++cases;
}
static uint32_t random_word(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *state = x;
}
int main(void) {
  static const uint32_t edges[] = {0,1,2,3,7,15,31,255,1023,2047,4095,
      65535,65536,0x7ffffffe,0x7fffffff,0x80000000,0xfffffffd,0xfffffffe,0xffffffff};
  uint32_t d, n, seed = 0x12345678, sentinel = 123;
  unsigned i, j;
  evaluator e;
  uintptr_t q, r;
  CHECK(!xe_gpu_divide_prepare(0, &sentinel) && sentinel == 123);
  CHECK(!xe_gpu_divide_prepare(3, NULL));
  for (d = 1; d < 1024; ++d) {
    for (n = 0; n < 4096; ++n) check_pair(n, d);
    for (j = 0; j < sizeof(edges) / sizeof(edges[0]); ++j) {
      check_pair(edges[j], d);
      /* Quotient boundaries across the complete 32-bit input range. */
      n = edges[j] - edges[j] % d;
      check_pair(n, d); if (n) check_pair(n - 1, d);
      if (n != UINT32_MAX) check_pair(n + 1, d);
    }
  }
  for (i = 0; i < 1000000; ++i) {
    n = random_word(&seed); d = random_word(&seed);
    check_pair(n, d ? d : 1);
  }
  for (i = 1; i <= 12; ++i) {
    memset(&e, 0, sizeof(e)); e.count = 4; e.fail_at = i;
    e.values[1] = UINT32_MAX; e.values[2] = 3;
    CHECK(xe_gpu_divide_prepare(3, &e.values[3]));
    q = 99; r = 98;
    CHECK(!xe_gpu_divide_emit(evaluate, &e, 1, 2, 3, &q, &r));
    CHECK(q == 99 && r == 98 && e.calls == i);
  }
  CHECK(!xe_gpu_divide_emit(NULL, &e, 1, 2, 3, &q, &r));
  CHECK(!xe_gpu_divide_emit(evaluate, &e, 0, 2, 3, &q, &r));
  CHECK(!xe_gpu_divide_emit(evaluate, &e, 1, 0, 3, &q, &r));
  CHECK(!xe_gpu_divide_emit(evaluate, &e, 1, 2, 0, &q, &r));
  CHECK(!xe_gpu_divide_emit(evaluate, &e, 1, 2, 3, NULL, &r));
  CHECK(!xe_gpu_divide_emit(evaluate, &e, 1, 2, 3, &q, NULL));
  printf("PASS: %u exact u32 GPU div/mod pairs; all 12 failure boundaries\n", cases);
  return 0;
}
