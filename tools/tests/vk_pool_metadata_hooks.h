/* Test-only allocator hooks. Never included by production build rules. */
#ifndef XE_TEST_POOL_METADATA_HOOKS_H
#define XE_TEST_POOL_METADATA_HOOKS_H
#include <stddef.h>
#include <stdlib.h>
void *xe_test_pool_metadata_alloc(size_t bytes);
#define XE_POOL_OWNER_ALLOC xe_test_pool_metadata_alloc
#define XE_POOL_OWNER_FREE free
#define XE_POOL_SEARCH_ALLOC xe_test_pool_metadata_alloc
#define XE_POOL_SEARCH_FREE free
#endif
