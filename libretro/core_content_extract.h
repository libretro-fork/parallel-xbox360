/* Synchronous extraction into a core-owned directory. Source callbacks and
 * views are borrowed until return; they never escape into a worker or queue. */
#ifndef XE_CORE_CONTENT_EXTRACT_H
#define XE_CORE_CONTENT_EXTRACT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_content_source {
  void *opaque;
  uint64_t size;
  /* Optional immutable whole-file view; NULL selects bounded ordinary reads. */
  const uint8_t *(*view)(void *opaque, uint64_t *available);
  int (*read)(void *opaque, uint64_t offset, void *out,
              size_t wanted, size_t *received);
} xe_content_source;
/* Reject paths/streams masquerading as one guest filename. name is a span. */
int xe_content_leaf_valid(const char *name, size_t length);
/* Reuse a same-sized regular destination; otherwise stage, check and rename.
 * Failure preserves the previous destination and attempts to remove only this
 * operation's staging file. Native/backend rename semantics still apply.
 * No payload heap allocation; unmappable input reuses a 64 KiB call-frame span.
 * Small paths use inline storage; long paths may allocate metadata only. */
int xe_content_extract_file(const xe_content_source *source,
    const char *directory, const char *name, size_t name_length);
#ifdef __cplusplus
}
#endif
#endif
