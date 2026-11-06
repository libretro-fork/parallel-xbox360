/* Owned reusable bytes; zero-initialize and never shallow-copy a live owner. */
#ifndef XE_CORE_BYTE_STORAGE_H
#define XE_CORE_BYTE_STORAGE_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_BYTES_REALLOC
#define XE_BYTES_REALLOC realloc
#define XE_BYTES_FREE free
#endif
typedef struct xe_byte_storage { uint8_t *data; size_t size, capacity; } xe_byte_storage;
static INLINE int xe_bytes_resize(xe_byte_storage *s, size_t size) {
  uint8_t *data;
  size_t capacity;
  if (!s) return 0;
  if (size > s->capacity) {
    capacity = s->capacity;
    if (capacity <= (size_t)-1 / 2) capacity *= 2;
    if (capacity < size) capacity = size;
    data = (uint8_t *)XE_BYTES_REALLOC(s->data, capacity);
    if (!data) return 0;
    s->data = data; s->capacity = capacity;
  }
  s->size = size; return 1;
}
/* Patch growth deliberately preserves vector's zero-initialized new tail. */
static INLINE int xe_bytes_resize_zero(xe_byte_storage *s, size_t size) {
  size_t old = s->size;
  if (!xe_bytes_resize(s, size)) return 0;
  if (size > old) memset(s->data + old, 0, size - old);
  return 1;
}
static INLINE void xe_bytes_free(xe_byte_storage *s) {
  XE_BYTES_FREE(s->data); s->data = NULL; s->size = s->capacity = 0;
}
#endif
