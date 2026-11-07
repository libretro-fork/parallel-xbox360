/* Test-only ABI shared by the C89 trace and native emitter bridges. */
#ifndef XE_TEST_RESOLVE_LOADS_EMIT_H
#define XE_TEST_RESOLVE_LOADS_EMIT_H
#include <stddef.h>
#include <stdint.h>
typedef struct xe_test_resolve_options {
  unsigned format, depth, msaa, scale, config, direct;
} xe_test_resolve_options;
typedef int (*xe_test_resolve_inspect)(void *, const uint32_t *, size_t);
#ifdef __cplusplus
extern "C" {
#endif
int xe_test_resolve_spv(const xe_test_resolve_options *, xe_test_resolve_inspect, void *);
#ifdef __cplusplus
}
#endif
#endif
