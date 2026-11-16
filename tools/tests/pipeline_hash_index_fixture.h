/* Test-only C boundary for actual typed pipeline owners and key equality. */
#ifndef XE_TEST_PIPELINE_HASH_INDEX_FIXTURE_H
#define XE_TEST_PIPELINE_HASH_INDEX_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
typedef struct xe_pipeline_index_key {uint64_t words[8];} xe_pipeline_index_key;
typedef struct xe_pipeline_index_fixture {
 unsigned version,bytes,intrusive,tracked_allocations;
 size_t owner_bytes;
 void *(*open)(void);
 void (*clear)(void*);
 void (*close)(void*);
 void *(*owner_new)(const xe_pipeline_index_key*);
 void (*owner_free)(void*);
 int (*insert)(void*,uint64_t,void*);
 /* Query an existing native owner's immutable key, borrowed through return. */
 void *(*find)(void*,uint64_t,const void*);
 size_t (*count)(void*);
 size_t (*bucket_bytes)(void*);
 void (*allocation_reset)(void);
 size_t (*allocation_count)(void);
 size_t (*live_allocation_count)(void);
} xe_pipeline_index_fixture;
typedef const xe_pipeline_index_fixture *(*xe_pipeline_index_fixture_get)(void);
#ifdef __cplusplus
extern "C" {
#endif
/* Only instrumented correctness modules link these C89 counters. */
void *xe_pipeline_index_test_allocate(size_t);
void xe_pipeline_index_test_free(void*);
void xe_pipeline_index_test_reset(void);
size_t xe_pipeline_index_test_count(void);
size_t xe_pipeline_index_test_live(void);
#ifdef __cplusplus
}
#endif
#endif
