#ifndef XE_CORE_CACHE_WRITER_H
#define XE_CORE_CACHE_WRITER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_cache_writer xe_cache_writer;
typedef void (*xe_cache_writer_sink)(void *opaque, unsigned kind,
      const void *data, size_t size);
/* One producer, one owned file worker. All storage is allocated at creation.
 * The sink borrows a record until it returns; it must not reenter the writer.
 * A full queue applies backpressure rather than losing persistence records. */
xe_cache_writer *xe_cache_writer_new(size_t max_record, size_t slots,
      xe_cache_writer_sink sink, void *opaque);
int xe_cache_writer_submit(xe_cache_writer *writer, unsigned kind,
      const void *data, size_t size);
/* Assemble two byte ranges directly into one owned record, without a temporary
 * concatenation buffer. Both ranges are copied before this call returns. */
int xe_cache_writer_submit_parts(xe_cache_writer *writer, unsigned kind,
      const void *first, size_t first_size, const void *second, size_t second_size);
/* Producer must be quiescent. Drains, joins, then frees queue storage. */
void xe_cache_writer_free(xe_cache_writer *writer);
#ifdef __cplusplus
}
#endif
#endif
