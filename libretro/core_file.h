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
/* Variable-size borrowed ranges. Views expire at the next get or close.
 * Mapping needs no payload allocation; fallback retains its largest range.
 * Zero-size ranges return a non-NULL sentinel after bounds validation. */
typedef struct xe_file_ranges xe_file_ranges;
xe_file_ranges *xe_file_ranges_open(const char *path);
uint64_t xe_file_ranges_size(const xe_file_ranges *ranges);
const uint8_t *xe_file_ranges_get(xe_file_ranges *ranges, uint64_t offset, size_t size);
void xe_file_ranges_close(xe_file_ranges *ranges);
/* Fixed-record snapshot. Borrowed bytes expire at the next get or close.
 * Native files map directly; other VFS inputs reuse a bounded 64 KiB window
 * (or one record when larger). Trailing incomplete records are ignored. */
typedef struct xe_file_records xe_file_records;
xe_file_records *xe_file_records_open(const char *path, uint64_t start, size_t record_size);
size_t xe_file_records_count(const xe_file_records *records);
size_t xe_file_records_stride(const xe_file_records *records);
const uint8_t *xe_file_records_get(xe_file_records *records, size_t index);
int xe_file_records_read(xe_file_records *records, size_t index, void *out, size_t size);
void xe_file_records_close(xe_file_records *records);
#ifdef __cplusplus
}
#endif
#endif
