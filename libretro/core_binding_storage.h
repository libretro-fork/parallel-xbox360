/* Immutable packed shader-binding ownership. MSVC C89; BSD license. */
#ifndef XE_CORE_BINDING_STORAGE_H
#define XE_CORE_BINDING_STORAGE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_binding_storage {
   void *data;
   size_t texture_count, sampler_count;
} xe_binding_storage;
/* Zero initialized, exclusively owned, four-byte native records. Failure leaves
 * the owner unchanged. Storage stays fixed until all borrowed readers finish. */
int xe_binding_storage_allocate(xe_binding_storage *storage,
      size_t textures, size_t samplers);
void xe_binding_storage_reset(xe_binding_storage *storage);
#ifdef __cplusplus
}
#endif
#endif
