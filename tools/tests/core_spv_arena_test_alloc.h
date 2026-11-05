#ifndef XE_SPV_ARENA_TEST_ALLOC_H
#define XE_SPV_ARENA_TEST_ALLOC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *xe_spv_test_alloc(size_t size);
void xe_spv_test_free(void *value);
size_t xe_spv_test_live(void);
void xe_spv_test_fail_next(void);
void *xe_spv_test_page_alloc(size_t size);
void xe_spv_test_page_free(void *value);
size_t xe_spv_test_pages(void);
size_t xe_spv_test_page_calls(void);
void xe_spv_test_page_fail_after(size_t successful_calls);
#ifdef __cplusplus
}
#endif
#define XE_SPV_ALLOC xe_spv_test_alloc
#define XE_SPV_FREE xe_spv_test_free
#define XE_SPV_ARENA_ALLOC xe_spv_test_page_alloc
#define XE_SPV_ARENA_FREE xe_spv_test_page_free
#endif
