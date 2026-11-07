/* Exact unsigned GPU div/mod using a CPU-prepared floor reciprocal. C89.
 * No floating point, shader int64, dynamic division or control flow. */
#ifndef XE_CORE_GPU_DIVIDE_H
#define XE_CORE_GPU_DIVIDE_H
#include <stdint.h>
#include "../third_party/libretro-common/include/retro_inline.h"

/* Zero encodes divisor one. A zero divisor is invalid and leaves output alone. */
static INLINE int xe_gpu_divide_prepare(uint32_t divisor, uint32_t *reciprocal) {
  if (!divisor || !reciprocal) return 0;
  *reciprocal = (uint32_t)(UINT64_C(0x100000000) / divisor);
  return 1;
}
enum xe_gpu_divide_op {
  XE_GPU_DIV_CONST, XE_GPU_DIV_MUL_HIGH, XE_GPU_DIV_EQ, XE_GPU_DIV_MUL,
  XE_GPU_DIV_SUB, XE_GPU_DIV_GE, XE_GPU_DIV_ADD, XE_GPU_DIV_SELECT
};
/* IDs are nonzero opaque native IR values. CONST receives its literal in a;
 * SELECT receives condition/true/false. Callback zero means emission failure. */
typedef uintptr_t (*xe_gpu_divide_emit_fn)(void *, enum xe_gpu_divide_op,
    uintptr_t, uintptr_t, uintptr_t);
static INLINE int xe_gpu_divide_emit(xe_gpu_divide_emit_fn emit, void *context,
    uintptr_t numerator, uintptr_t divisor, uintptr_t reciprocal,
    uintptr_t *quotient, uintptr_t *remainder) {
  uintptr_t zero, one, high, identity, q, r, correction, increment, decrement;
  uintptr_t product, result_q, result_r;
  if (!emit || !numerator || !divisor || !reciprocal || !quotient || !remainder)
    return 0;
#define XE_GPU_DIV_STEP(out, op, a, b, c) do { \
  out = emit(context, op, a, b, c); if (!out) return 0; \
} while (0)
  XE_GPU_DIV_STEP(zero, XE_GPU_DIV_CONST, 0, 0, 0);
  XE_GPU_DIV_STEP(one, XE_GPU_DIV_CONST, 1, 0, 0);
  XE_GPU_DIV_STEP(high, XE_GPU_DIV_MUL_HIGH, numerator, reciprocal, 0);
  XE_GPU_DIV_STEP(identity, XE_GPU_DIV_EQ, reciprocal, zero, 0);
  XE_GPU_DIV_STEP(q, XE_GPU_DIV_SELECT, identity, numerator, high);
  /* floor(2^32 / d) underestimates by less than 1 / 2^32. For any u32
   * numerator the initial quotient is exact or one too small; q*d <= n,
   * so neither the product nor the initial remainder overflows. */
  XE_GPU_DIV_STEP(product, XE_GPU_DIV_MUL, q, divisor, 0);
  XE_GPU_DIV_STEP(r, XE_GPU_DIV_SUB, numerator, product, 0);
  XE_GPU_DIV_STEP(correction, XE_GPU_DIV_GE, r, divisor, 0);
  XE_GPU_DIV_STEP(increment, XE_GPU_DIV_ADD, q, one, 0);
  XE_GPU_DIV_STEP(decrement, XE_GPU_DIV_SUB, r, divisor, 0);
  XE_GPU_DIV_STEP(result_q, XE_GPU_DIV_SELECT, correction, increment, q);
  XE_GPU_DIV_STEP(result_r, XE_GPU_DIV_SELECT, correction, decrement, r);
#undef XE_GPU_DIV_STEP
  *quotient = result_q; *remainder = result_r;
  return 1;
}
#endif
