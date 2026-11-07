#include <stddef.h>
void *xe_gpu_task_test_alloc(size_t,size_t);
void xe_gpu_task_test_free(void *);
void xe_gpu_task_test_held(void *);
#define XE_GPU_TASK_ALLOC xe_gpu_task_test_alloc
#define XE_GPU_TASK_FREE xe_gpu_task_test_free
#define XE_GPU_TASK_TEST_HELD 1
