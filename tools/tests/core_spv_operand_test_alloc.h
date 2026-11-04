#ifndef XE_SPV_OPERAND_TEST_ALLOC_H
#define XE_SPV_OPERAND_TEST_ALLOC_H
#include <stddef.h>
void *xe_spv_test_alloc(size_t size);
void xe_spv_test_free(void *value);
#define XE_SPV_ALLOC xe_spv_test_alloc
#define XE_SPV_FREE xe_spv_test_free
#endif
