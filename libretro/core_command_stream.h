/* Single-owner aligned command packets. Pages and packet addresses remain
 * stable until reset/destroy; reset retains storage without clearing payloads. */
#ifndef XE_CORE_COMMAND_STREAM_H
#define XE_CORE_COMMAND_STREAM_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#ifndef XE_COMMAND_STREAM_ALLOC
#define XE_COMMAND_STREAM_ALLOC malloc
#define XE_COMMAND_STREAM_FREE free
#endif
typedef struct xe_command_page {
  struct xe_command_page *next;
  size_t capacity, used;
  uintmax_t alignment;
} xe_command_page;
typedef struct xe_command_stream {
  xe_command_page *first, *last, *current;
  size_t page_size, size;
} xe_command_stream;
static INLINE size_t xe_command_page_header_size(void) {
  return ((sizeof(xe_command_page) + sizeof(uintmax_t) - 1) / sizeof(uintmax_t)) * sizeof(uintmax_t);
}
static INLINE unsigned char *xe_command_page_data(xe_command_page *page) {
  return (unsigned char *)page + xe_command_page_header_size();
}
static INLINE const unsigned char *xe_command_page_const_data(const xe_command_page *page) {
  return (const unsigned char *)page + xe_command_page_header_size();
}
/* Preserve the native packet format: aligned header followed by an aligned
 * argument extent whose element count is representable in uint32_t. */
static INLINE int xe_command_packet_layout(size_t arguments, size_t header,
    uint32_t *argument_elements, size_t *packet_bytes) {
  size_t remainder = arguments % sizeof(uintmax_t);
  if (remainder) {
    size_t padding = sizeof(uintmax_t) - remainder;
    if (arguments > (size_t)-1 - padding) return 0;
    arguments += padding;
  }
  if (header % sizeof(uintmax_t) || arguments / sizeof(uintmax_t) > UINT32_MAX ||
      header > (size_t)-1 - arguments) return 0;
  *argument_elements = (uint32_t)(arguments / sizeof(uintmax_t));
  *packet_bytes = header + arguments;
  return 1;
}
static INLINE xe_command_page *xe_command_page_create(size_t capacity) {
  xe_command_page *page;
  size_t header = xe_command_page_header_size();
  if (!capacity || capacity > (size_t)-1 - header) return NULL;
  page = (xe_command_page *)XE_COMMAND_STREAM_ALLOC(header + capacity);
  if (!page) return NULL;
  page->next = NULL; page->capacity = capacity; page->used = 0;
  return page;
}
/* Zero-initialize the owner. Initialization failure publishes no storage. */
static INLINE int xe_command_stream_init(xe_command_stream *s, size_t page_size) {
  xe_command_page *page;
  if (s->first || !page_size) return 0;
  page = xe_command_page_create(page_size);
  if (!page) return 0;
  s->first = s->last = s->current = page;
  s->page_size = page_size; s->size = 0;
  return 1;
}
/* A packet never straddles pages. New high-water pages are owned without
 * reallocating/copying earlier commands. Failure preserves the whole stream. */
static INLINE void *xe_command_stream_append(xe_command_stream *s, size_t bytes) {
  xe_command_page *page = s->current;
  unsigned char *packet;
  if (!page || !bytes || bytes % sizeof(uintmax_t) || bytes > (size_t)-1 - s->size) return NULL;
  if (bytes > page->capacity - page->used) {
    page = page->next;
    while (page && bytes > page->capacity) page = page->next;
    if (!page) {
      size_t capacity = bytes > s->page_size ? bytes : s->page_size;
      page = xe_command_page_create(capacity);
      if (!page) return NULL;
      s->last->next = page; s->last = page;
    }
    s->current = page;
  }
  packet = xe_command_page_data(page) + page->used;
  page->used += bytes; s->size += bytes;
  return packet;
}
static INLINE void xe_command_stream_reset(xe_command_stream *s) {
  xe_command_page *page;
  for (page = s->first; page; page = page->next) page->used = 0;
  s->current = s->first; s->size = 0;
}
static INLINE void xe_command_stream_destroy(xe_command_stream *s) {
  xe_command_page *page = s->first;
  while (page) {
    xe_command_page *next = page->next;
    XE_COMMAND_STREAM_FREE(page); page = next;
  }
  s->first = s->last = s->current = NULL; s->page_size = s->size = 0;
}
#ifdef __cplusplus
struct xe_command_stream_owner : xe_command_stream {
  xe_command_stream_owner() : xe_command_stream{} {}
  xe_command_stream_owner(const xe_command_stream_owner&) = delete;
  xe_command_stream_owner& operator=(const xe_command_stream_owner&) = delete;
  ~xe_command_stream_owner() { xe_command_stream_destroy(this); }
};
#endif
#endif
