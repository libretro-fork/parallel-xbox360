/* Retired immutable snapshot storage. BSD license. MSVC C89.
 * A view remains exclusive to its owner until every consumer has finished.
 * Returning a live or borrowed payload to the reuse slot is invalid. */
#ifndef XE_CORE_SHADER_UCODE_REUSE_H
#define XE_CORE_SHADER_UCODE_REUSE_H
#include <stddef.h>
#include <stdint.h>
#define XE_SHADER_UCODE_LARGE_ALIGNMENT 32
#define XE_SHADER_UCODE_REUSE_AVAILABLE 1
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_shader_ucode_reuse {
 uint32_t *words;
 size_t count;
} xe_shader_ucode_reuse;
int xe_shader_ucode_reuse_allocate(xe_shader_ucode_reuse *owner,size_t count);
void xe_shader_ucode_reuse_close(xe_shader_ucode_reuse *owner);
/* Drain after the content's shader/worker owners have been destroyed. */
void xe_shader_ucode_reuse_clear(void);
#ifdef __cplusplus
}
#endif
#endif
