/* C89 conditional selection ABI. BSD license; see LICENSE. */
#include "core_alu_operand.h"
#if defined(_MSC_VER)
#define XE_CONDITIONAL_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XE_CONDITIONAL_NOINLINE __attribute__((noinline))
#else
#error Conditional dispatcher requires a supported noinline attribute
#endif

/* Preserve this boundary under LTO too. Register-heavy CND code must not expand
 * the save/live-register set of unrelated scalar instructions in the caller.
 * Whether this corrects the observed scalar regression requires measurement. */
XE_CONDITIONAL_NOINLINE void xe_alu_conditional_dispatch(float *output,
 const float *condition,const float *yes,const float *no,unsigned mode) {
 xe_alu_conditional_vector(output,condition,yes,no,mode);
}
