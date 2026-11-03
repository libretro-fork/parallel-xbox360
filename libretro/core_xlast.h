#ifndef XE_CORE_XLAST_H
#define XE_CORE_XLAST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_xlast xe_xlast;
typedef struct rxml_node xe_xlast_query;
typedef void (*xe_xlast_sink)(void *opaque, uint32_t value);
/* Takes ownership on success and failure. Input must have len+1 bytes with
 * input[len]==0. UTF-8 is parsed in place; other detected XML encodings are
 * transcoded once before parsing. Returned strings/query nodes are borrowed
 * until xe_xlast_destroy. No file I/O or runtime compiler is involved. */
xe_xlast *xe_xlast_parse_owned(char *input, size_t len);
xe_xlast *xe_xlast_create_gzip(const uint8_t *input, size_t size, size_t decoded);
void xe_xlast_destroy(xe_xlast *doc);
const char *xe_xlast_title(const xe_xlast *doc);
unsigned xe_xlast_product(const xe_xlast *doc, uint32_t values[7]);
size_t xe_xlast_languages(const xe_xlast *doc, xe_xlast_sink sink, void *opaque);
const char *xe_xlast_localized(const xe_xlast *doc, uint32_t id, uint32_t language);
int xe_xlast_presence(const xe_xlast *doc, uint32_t id, uint32_t *value);
int xe_xlast_property(const xe_xlast *doc, uint32_t id, uint32_t *value);
int xe_xlast_context(const xe_xlast *doc, uint32_t id, uint32_t context, uint32_t *value);
const xe_xlast_query *xe_xlast_find_query(const xe_xlast *doc, uint32_t id);
const char *xe_xlast_query_name(const xe_xlast_query *query);
/* kind: 0 returns, 1 parameters, 2 filters. Only final caller storage is
 * populated through sink; a NULL sink counts values without staging them. */
size_t xe_xlast_query_values(const xe_xlast_query *query, unsigned kind,
    xe_xlast_sink sink, void *opaque);
#ifdef __cplusplus
}
#endif
#endif
