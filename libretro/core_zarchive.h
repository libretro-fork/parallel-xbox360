/* Immutable ZArchive reader. Names borrow the reader's lifetime. Reads share
 * an rthreads-protected decoder/cache; quiesce all operations before close. */
#ifndef XE_CORE_ZARCHIVE_H
#define XE_CORE_ZARCHIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_zar xe_zar;
#define XE_ZAR_MAGIC 0x169f52d6U
#define XE_ZAR_INVALID ((uint32_t)0xffffffffU)
typedef struct xe_zar_entry {
  const char *name;
  size_t name_size;
  uint64_t size;
  int is_file;
} xe_zar_entry;
xe_zar *xe_zar_open(const char *path);
void xe_zar_close(xe_zar *reader);
uint32_t xe_zar_lookup(const xe_zar *reader, const char *path, size_t length,
    int allow_file, int allow_directory);
int xe_zar_type(const xe_zar *reader, uint32_t node);
uint32_t xe_zar_count(const xe_zar *reader, uint32_t node);
int xe_zar_entry_get(const xe_zar *reader, uint32_t node, uint32_t index,
    xe_zar_entry *entry);
uint64_t xe_zar_size(const xe_zar *reader, uint32_t node);
/* Borrow only immutable, contiguous raw mapped storage, never cache entries.
 * A nonempty exact range is required; retain reader until the view is unused. */
const uint8_t *xe_zar_view(const xe_zar *reader, uint32_t node,
    uint64_t offset, size_t length);
/* Exact bounded prefix, capped at EOF. Failure may modify output. */
uint64_t xe_zar_read(xe_zar *reader, uint32_t node, uint64_t offset,
    uint64_t length, void *output);
#ifdef XE_ZAR_TEST
size_t xe_zar_test_cache_bytes(const xe_zar *reader);
size_t xe_zar_test_metadata_bytes(const xe_zar *reader);
#endif
#ifdef __cplusplus
}
#endif
#endif
