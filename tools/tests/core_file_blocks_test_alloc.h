#ifndef XE_BLOCK_TEST_ALLOC_H
#define XE_BLOCK_TEST_ALLOC_H
#include <stddef.h>
void *xe_block_test_alloc(size_t size);
void xe_block_test_free(void *value);
#define XE_BLOCK_ALLOC xe_block_test_alloc
#define XE_BLOCK_FREE xe_block_test_free
#endif
