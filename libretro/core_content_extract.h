/* Synchronous extraction into a core-owned directory. Source callbacks and
 * views are borrowed until return; they never escape into a worker or queue. */
#ifndef XE_CORE_CONTENT_EXTRACT_H
#define XE_CORE_CONTENT_EXTRACT_H
#include "core_content_input.h"
#ifdef __cplusplus
extern "C" {
#endif
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
