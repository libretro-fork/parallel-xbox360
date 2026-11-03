/* ARM64 user-mode register reads. MSVC-compatible C89; BSD license. */
#ifndef XENIA_BASE_ARM64_SYSREG_H_
#define XENIA_BASE_ARM64_SYSREG_H_

#include <stdint.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define XE_ARM64_REG_INLINE static __inline
#define XE_ARM64_REG_READ(msvc_reg, name) \
  return (uint64_t)_ReadStatusReg(msvc_reg)
#elif defined(__clang__)
#include <arm_acle.h>
#define XE_ARM64_REG_INLINE static __inline__
#define XE_ARM64_REG_READ(msvc_reg, name) \
  return (uint64_t)__arm_rsr64(name)
#elif defined(__GNUC__)
#define XE_ARM64_REG_INLINE static __inline__
#define XE_ARM64_REG_READ(msvc_reg, name) \
  uint64_t value; \
  __asm__ __volatile__("mrs %0, " name : "=r"(value)); \
  return value
#else
#error Unsupported ARM64 compiler
#endif

/* Encodings match ARM64_SYSREG(3, 3, CRn, CRm, op2) on Windows. */
XE_ARM64_REG_INLINE uint64_t xe_arm64_read_dczid_el0(void) {
  XE_ARM64_REG_READ(0x5807, "dczid_el0");
}

XE_ARM64_REG_INLINE uint64_t xe_arm64_read_cntfrq_el0(void) {
  XE_ARM64_REG_READ(0x5f00, "cntfrq_el0");
}

XE_ARM64_REG_INLINE uint64_t xe_arm64_read_cntvct_el0(void) {
  XE_ARM64_REG_READ(0x5f02, "cntvct_el0");
}

#undef XE_ARM64_REG_INLINE
#undef XE_ARM64_REG_READ
#endif
