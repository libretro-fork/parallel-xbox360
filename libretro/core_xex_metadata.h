/* Bounded, allocation-free XEX metadata parser. Source lifetime covers parsing
 * and consumption of the returned name range. Callbacks must read exactly. */
#ifndef XE_CORE_XEX_METADATA_H
#define XE_CORE_XEX_METADATA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_xex_source {
  void *context;
  uint64_t size;
  int (*read)(void *, uint64_t, void *, size_t);
  const uint8_t *(*view)(void *, uint64_t, size_t);
} xe_xex_source;
typedef struct xe_xex_metadata {
  uint32_t format, title_id, media_id, savegame_id, version, base_version;
  uint32_t name_offset, name_size;
  uint8_t disc_number, disc_count, ratings_present;
  uint8_t esrb, pegi, cero, usk, oflc_au, oflc_nz;
} xe_xex_metadata;
/* Failure leaves output untouched. Unrecognized optional keys are ignored. */
int xe_xex_parse(const xe_xex_source *source, xe_xex_metadata *output);
#ifdef __cplusplus
}
#endif
#endif
