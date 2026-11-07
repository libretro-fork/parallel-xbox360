/* GPU-owner retained rows. Submitted spans stay borrowed until reset/close.
 * MSVC C89; storage grows by pages, never by relocating or copying live rows.
 * Rows may not require destruction on reset/close. */
#ifndef XE_CORE_COMMAND_ROWS_H
#define XE_CORE_COMMAND_ROWS_H
#include "core_command_stream.h"
typedef struct xe_command_rows {
  xe_command_stream stream;
  xe_command_page *read_page;
  size_t read_offset, stride, pending;
} xe_command_rows;
typedef struct xe_command_row_view {
  const void *data;
  xe_command_page *page;
  size_t offset;
  uint32_t count;
} xe_command_row_view;
static INLINE int xe_command_rows_init(xe_command_rows *q, size_t stride, size_t page_size) {
  if (q->stream.first || !stride || stride % sizeof(uintmax_t)) return 0;
  if (page_size < stride) page_size = stride;
  page_size -= page_size % stride;
  if (!xe_command_stream_init(&q->stream, page_size)) return 0;
  q->read_page = q->stream.first; q->read_offset = q->pending = 0;
  q->stride = stride; return 1;
}
static INLINE void *xe_command_rows_append(xe_command_rows *q) {
  void *row;
  if (!q->stride || q->pending == (size_t)-1) return NULL;
  row = xe_command_stream_append(&q->stream, q->stride);
  if (row) ++q->pending;
  return row;
}
/* Cancel only the newest unsubmitted row. No older borrowed row is modified. */
static INLINE int xe_command_rows_pop(xe_command_rows *q) {
  xe_command_page *page = q->stream.current;
  if (!q->pending || !page || page->used < q->stride) return 0;
  page->used -= q->stride; q->stream.size -= q->stride; --q->pending;
  return 1;
}
/* Peek/commit are separate so a failed command-packet allocation can retry
 * without losing rows. The single owner must not reset while a view is leased. */
static INLINE int xe_command_rows_peek(const xe_command_rows *q, uint32_t limit,
    xe_command_row_view *view) {
  xe_command_page *page = q->read_page;
  size_t offset = q->read_offset, count;
  if (!q->pending || !limit || !q->stride) return 0;
  while (page && offset == page->used) { page = page->next; offset = 0; }
  if (!page || offset > page->used || (page->used - offset) % q->stride) return 0;
  count = (page->used - offset) / q->stride;
  if (count > q->pending) count = q->pending;
  if (count > limit) count = limit;
  if (!count) return 0;
  view->data = xe_command_page_const_data(page) + offset;
  view->page = page; view->offset = offset; view->count = (uint32_t)count;
  return 1;
}
static INLINE int xe_command_rows_commit(xe_command_rows *q, const xe_command_row_view *view) {
  xe_command_row_view expected;
  if (!view->count || !xe_command_rows_peek(q, view->count, &expected) ||
      view->data != expected.data || view->page != expected.page ||
      view->offset != expected.offset || view->count != expected.count) return 0;
  q->read_page = view->page;
  q->read_offset = view->offset + (size_t)view->count * q->stride;
  q->pending -= view->count; return 1;
}
/* The emitter records only a borrowed pointer/count. Failed publication keeps
 * this span pending; previously committed spans stay published for retry. */
typedef int (*xe_command_row_emit_fn)(void *, const xe_command_row_view *);
static INLINE int xe_command_rows_submit(xe_command_rows *q, uint32_t limit,
    xe_command_row_emit_fn emit, void *context) {
  xe_command_row_view view;
  if (!emit || !limit) return 0;
  while (xe_command_rows_peek(q, limit, &view)) {
    if (!emit(context, &view) || !xe_command_rows_commit(q, &view)) return 0;
  }
  return q->pending == 0;
}
/* Preserve unsubmitted rows if a new submission starts with barriers already
 * pending. Otherwise the caller must have replayed/discarded all borrowed spans. */
static INLINE int xe_command_rows_reset(xe_command_rows *q) {
  if (q->pending) return 0;
  xe_command_stream_reset(&q->stream); q->read_page = q->stream.first;
  q->read_offset = 0; return 1;
}
static INLINE void xe_command_rows_close(xe_command_rows *q) {
  xe_command_stream_destroy(&q->stream);
  q->read_page = NULL; q->read_offset = q->stride = q->pending = 0;
}
#ifdef __cplusplus
struct xe_command_rows_owner : xe_command_rows {
  xe_command_rows_owner() : xe_command_rows{} {}
  xe_command_rows_owner(const xe_command_rows_owner&) = delete;
  xe_command_rows_owner& operator=(const xe_command_rows_owner&) = delete;
  ~xe_command_rows_owner() { xe_command_rows_close(this); }
};
#endif
#endif
