/* Single-owner immutable input; synchronize callers and retain through views. */
#ifndef XE_CORE_FILE_H
#define XE_CORE_FILE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_file xe_file;
xe_file *xe_file_open(const char *path);
void xe_file_close(xe_file *file);
uint64_t xe_file_size(const xe_file *file);
/* Complete range preflight precedes access. Failure may modify destination. */
int xe_file_read(xe_file *file, uint64_t offset, void *out, size_t size);
/* Immutable mapping valid until close; NULL selects ordinary reads. */
const uint8_t *xe_file_view(const xe_file *file, uint64_t offset, size_t size);
#ifdef __cplusplus
}
#endif
#endif
