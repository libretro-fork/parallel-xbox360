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
/* One synchronous, non-reentrant writer. Any append/reset/destroy invalidates
 * the lease; callers must commit or abandon before another stream operation.
 * Reservation may retain a new high-water page but publishes no packet bytes. */
typedef struct xe_command_reservation {
  xe_command_stream *owner;
  xe_command_page *page, *previous_current;
  size_t offset, bytes, previous_size;
} xe_command_reservation;
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
/* A failed payload writer simply abandons this lease. Existing packets remain
 * stable; no rollback copy or clearing of the unpublished payload is needed. */
static INLINE void *xe_command_stream_reserve(xe_command_stream *s, size_t bytes,
    xe_command_reservation *lease) {
  xe_command_page *page;
  if (!s || !lease || !s->current || !bytes || bytes % sizeof(uintmax_t) ||
      bytes > (size_t)-1 - s->size) return NULL;
  page = s->current;
  if (bytes > page->capacity - page->used) {
    page = page->next;
    while (page && bytes > page->capacity) page = page->next;
    if (!page) {
      size_t capacity = bytes > s->page_size ? bytes : s->page_size;
      page = xe_command_page_create(capacity);
      if (!page) return NULL;
      s->last->next = page; s->last = page;
    }
  }
  lease->owner = s; lease->page = page; lease->previous_current = s->current;
  lease->offset = page->used; lease->bytes = bytes; lease->previous_size = s->size;
  return xe_command_page_data(page) + page->used;
}
static INLINE int xe_command_stream_commit(xe_command_stream *s,
    xe_command_reservation *lease) {
  if (!s || !lease || lease->owner != s || !lease->page || !lease->bytes ||
      lease->bytes % sizeof(uintmax_t) ||
      s->current != lease->previous_current || s->size != lease->previous_size ||
      lease->page->used != lease->offset ||
      lease->bytes > lease->page->capacity - lease->offset ||
      lease->bytes > (size_t)-1 - s->size) return 0;
  lease->page->used += lease->bytes; s->size += lease->bytes;
  s->current = lease->page; lease->owner = NULL; lease->page = NULL;
  return 1;
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
