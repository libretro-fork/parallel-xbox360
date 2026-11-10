#ifndef XE_CORE_CACHE_FILE_H
#define XE_CORE_CACHE_FILE_H
#include <stddef.h>
#include <stdint.h>
#include <streams/file_stream.h>
#include "core_file.h"
#ifdef __cplusplus
extern "C" {
#endif
/* One owner per persistent cache file. All opening/repair happens before the
 * writer starts. Existing files are never retried with a truncating open. */
RFILE *xe_cache_file_open(const char *path);
size_t xe_cache_file_read(void *out, size_t size, size_t count, RFILE *file);
/* At position zero: 1 complete, 0 shorter file, -1 I/O/argument failure.
 * A failed read must not be treated as permission to replace a cache. */
int xe_cache_file_read_header(RFILE *file, void *out, size_t size);
size_t xe_cache_file_write(const void *data, size_t size, size_t count, RFILE *file);
/* Optional diagnostic sinks: checked path, exact borrowed transfer, no endian
 * conversion or payload staging. Relative directories stay relative. DXIL
 * containers may end between words; the byte API preserves their full tail. */
int xe_cache_file_dump_keyed_bytes(const char *directory, const char *prefix,
      uint32_t key, const char *suffix, const void *bytes, size_t size);
int xe_cache_file_dump_keyed_words(const char *directory, const char *prefix,
      uint32_t key, const char *suffix, const uint32_t *words, size_t count);
/* Encode host words to persistent guest-endian bytes using fixed scratch. */
int xe_cache_file_write_be32(RFILE *file, const uint32_t *words, size_t count);
/* Consume validated borrowed rows immediately; sink must not reenter records.
 * Hash mismatch ends the valid prefix successfully. I/O/sink errors return 0.
 * Field bounds are checked before any row is accessed. */
typedef int (*xe_cache_record_sink)(void *opaque, const uint8_t *row, size_t size);
int xe_cache_records_validate(xe_file_records *records, size_t record_size,
      size_t hash_offset, size_t payload_offset, size_t payload_size,
      xe_cache_record_sink sink, void *opaque, size_t *valid_count);
/* Preflight before allocating from untrusted cache record lengths. */
int xe_cache_file_has_bytes(RFILE *file, uint64_t length);
/* Repair to validated length, discard lookahead, and select append position. */
int xe_cache_file_repair(RFILE *file, uint64_t length);
#ifdef __cplusplus
}
#endif
#endif
