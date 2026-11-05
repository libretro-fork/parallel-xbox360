/* Command-processor-owned fixed scratch. Zero-initialize before init.
 * Init/close are cold operations. Stride is sizeof the caller's metadata type;
 * its alignment must fit malloc's alignment. Append returns raw object storage.
 * Reset invalidates borrowed views without moving/clearing their bytes. The
 * caller may use only types needing no destructor on reset/close. */
#ifndef XE_CORE_GPU_WORKLIST_H
#define XE_CORE_GPU_WORKLIST_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_gpu_worklist {
   void *data;
   size_t count, capacity, stride;
   int failed;
} xe_gpu_worklist;
int xe_gpu_worklist_init(xe_gpu_worklist *list, size_t capacity, size_t stride);
void xe_gpu_worklist_reset(xe_gpu_worklist *list);
void *xe_gpu_worklist_append(xe_gpu_worklist *list);
void xe_gpu_worklist_close(xe_gpu_worklist *list);
#ifdef __cplusplus
}
#endif
#endif
