/* Forced into CPU tests only; no release tracing or allocation counters. */
#ifndef XE_TEST_SPV_IMAGE_SINK_ALLOC_H
#define XE_TEST_SPV_IMAGE_SINK_ALLOC_H
#include <stddef.h>
void *xe_test_sink_realloc(void *, size_t);
void xe_test_sink_free(void *);
#define XE_SPV_SINK_REALLOC xe_test_sink_realloc
#define XE_SPV_SINK_FREE xe_test_sink_free
#define XE_SPV_BINARY_REALLOC xe_test_sink_realloc
#define XE_SPV_BINARY_FREE xe_test_sink_free
#endif
