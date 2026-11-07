/* Allocation hooks are test-only and forced into every C++ recorder TU. */
#ifndef XE_COMMAND_ROWS_PROFILE_H
#define XE_COMMAND_ROWS_PROFILE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *xe_test_command_alloc(size_t);
void xe_test_command_free(void *);
#ifdef __cplusplus
}
#endif
#define XE_COMMAND_STREAM_ALLOC xe_test_command_alloc
#define XE_COMMAND_STREAM_FREE xe_test_command_free
#endif
