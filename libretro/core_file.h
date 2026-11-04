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
/* Zero-initialize before open; close before reuse. Native mappings are borrowed
 * through the owner. A non-mappable VFS gets one exact allocation, filled once.
 * No partially read blob is published. All pointers expire at close. */
typedef struct xe_file_blob {
  const uint8_t *data;
  size_t size;
  xe_file *owner;
  void *storage;
} xe_file_blob;
int xe_file_blob_open(xe_file_blob *blob, const char *path);
void xe_file_blob_close(xe_file_blob *blob);
#ifdef __cplusplus
}
#endif
#endif
