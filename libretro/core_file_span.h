/* Borrowed backing storage must remain alive throughout access. */
#ifndef XE_CORE_FILE_SPAN_H
#define XE_CORE_FILE_SPAN_H
#include <stddef.h>
#include <string.h>
#include <retro_inline.h>
static INLINE int xe_file_span_range(size_t total, size_t base, size_t offset,
      size_t length, size_t *absolute) {
   if (base > total || offset > total - base || length > total - base - offset) return 0;
   if (absolute) *absolute = base + offset;
   return 1;
}
/* Mapping request: zero length means the remaining logical file. */
static INLINE int xe_file_span_slice(size_t total, size_t base, size_t logical,
      size_t offset, size_t requested, size_t *absolute, size_t *length) {
   size_t count;
   if (offset > logical) return 0;
   count = logical - offset;
   if (requested && requested < count) count = requested;
   if (!xe_file_span_range(total, base, offset, count, absolute)) return 0;
   if (length) *length = count;
   return 1;
}
/* Complete bounds preflight; failure leaves the destination unchanged. */
static INLINE int xe_file_span_read(const void *source, size_t total, size_t base,
      size_t offset, void *destination, size_t length) {
   size_t absolute;
   if (!xe_file_span_range(total, base, offset, length, &absolute) ||
         (length && (!source || !destination))) return 0;
   if (length) memmove(destination, (const unsigned char *)source + absolute, length);
   return 1;
}
#endif
