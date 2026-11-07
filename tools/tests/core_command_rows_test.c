/* Retained row identities, transactional publication and teardown; MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations, live, fail_next;
static void *row_alloc(size_t bytes) {
  void *p;
  if (fail_next) { fail_next = 0; return NULL; }
  ++allocations; p = malloc(bytes); if (p) { ++live; memset(p, 0xa5, bytes); } return p;
}
static void row_free(void *p) { if (p) { --live; free(p); } }
#define XE_COMMAND_STREAM_ALLOC row_alloc
#define XE_COMMAND_STREAM_FREE row_free
#include "core_command_rows.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
typedef struct row { uint64_t value[4]; } row;
static row *saved[8192];
static void fill(row *r, size_t id) {
  size_t j; for (j = 0; j < 4; ++j) r->value[j] = (uint64_t)id * 37 + j * 53;
}
static void verify(const row *r, size_t id) {
  size_t j; for (j = 0; j < 4; ++j) CHECK(r->value[j] == (uint64_t)id * 37 + j * 53);
}
static void lifetime(void) {
  xe_command_rows q;
  xe_command_row_view view, stale;
  size_t i, seen = 0, before;
  memset(&q, 0, sizeof(q));
  fail_next = 1; CHECK(!xe_command_rows_init(&q, sizeof(row), 256) && !live);
  CHECK(!xe_command_rows_init(&q, 3, 256));
  CHECK(xe_command_rows_init(&q, sizeof(row), 256));
  CHECK(!xe_command_rows_init(&q, sizeof(row), 256));
  for (i = 0; i < 8192; ++i) {
    row *r = (row *)xe_command_rows_append(&q); CHECK(r); fill(r, i); saved[i] = r;
    if ((i & 7) == 7) {
      CHECK(xe_command_rows_peek(&q, 3, &view));
      CHECK(view.data == saved[seen]); stale = view;
      CHECK(xe_command_rows_commit(&q, &view)); seen += view.count;
      CHECK(!xe_command_rows_commit(&q, &stale));
    }
  }
  while (xe_command_rows_peek(&q, UINT32_MAX, &view)) {
    CHECK(view.data == saved[seen]);
    for (i = 0; i < view.count; ++i) verify((const row *)view.data + i, seen + i);
    CHECK(xe_command_rows_commit(&q, &view)); seen += view.count;
  }
  CHECK(seen == 8192 && !q.pending);
  for (i = 0; i < 8192; ++i) verify(saved[i], i); /* Already-published rows stay alive. */
  before = allocations;
  for (i = 0; i < 100; ++i) {
    size_t n;
    CHECK(xe_command_rows_reset(&q));
    for (n = 0; n < 8192; ++n) { row *r = (row *)xe_command_rows_append(&q); CHECK(r == saved[n]); fill(r, n); }
    while (xe_command_rows_peek(&q, UINT32_MAX, &view)) CHECK(xe_command_rows_commit(&q, &view));
  }
  CHECK(allocations == before);
  xe_command_rows_close(&q); CHECK(!live);
  xe_command_rows_close(&q); CHECK(!live && !xe_command_rows_append(&q));
}
static void rollback_and_faults(void) {
  xe_command_rows q; xe_command_row_view view, retry;
  row *r; size_t i, original_size;
  memset(&q, 0, sizeof(q)); CHECK(xe_command_rows_init(&q, sizeof(row), sizeof(row) * 2));
  r = (row *)xe_command_rows_append(&q); CHECK(r); fill(r, 0);
  CHECK(xe_command_rows_peek(&q, 1, &view));
  CHECK(!xe_command_rows_reset(&q)); verify(r, 0);
  /* An unsuccessful packet write leaves the peek uncommitted for retry. */
  CHECK(xe_command_rows_peek(&q, 1, &retry) && retry.data == view.data);
  retry.count = 2; CHECK(!xe_command_rows_commit(&q, &retry) && q.pending == 1);
  CHECK(xe_command_rows_commit(&q, &view));
  CHECK(!xe_command_rows_pop(&q));
  for (i = 1; i <= 2; ++i) { row *n = (row *)xe_command_rows_append(&q); CHECK(n); fill(n, i); }
  CHECK(xe_command_rows_pop(&q)); /* Last row of a newly opened page. */
  CHECK(xe_command_rows_peek(&q, UINT32_MAX, &view) && view.count == 1);
  verify((const row *)view.data, 1); CHECK(xe_command_rows_commit(&q, &view));
  CHECK(!xe_command_rows_peek(&q, UINT32_MAX, &view));
  r = (row *)xe_command_rows_append(&q); CHECK(r); fill(r, 2);
  CHECK(xe_command_rows_peek(&q, UINT32_MAX, &view)); verify((const row *)view.data, 2);
  CHECK(xe_command_rows_commit(&q, &view));
  r = (row *)xe_command_rows_append(&q); CHECK(r); fill(r, 3);
  original_size = q.stream.size; fail_next = 1;
  CHECK(!xe_command_rows_append(&q) && q.pending == 1 && q.stream.size == original_size);
  verify(r, 3); CHECK(xe_command_rows_peek(&q, 1, &view));
  r = (row *)xe_command_rows_append(&q); CHECK(r); fill(r, 4);
  verify((const row *)view.data, 3); CHECK(xe_command_rows_commit(&q, &view));
  CHECK(xe_command_rows_peek(&q, 1, &view)); verify((const row *)view.data, 4);
  CHECK(xe_command_rows_commit(&q, &view));
  q.pending = (size_t)-1; CHECK(!xe_command_rows_append(&q)); q.pending = 0;
  CHECK(xe_command_rows_reset(&q)); xe_command_rows_close(&q); CHECK(!live);
}
struct sink { unsigned seen, reject; };
static int emit(void *opaque, const xe_command_row_view *view) {
  struct sink *sink = (struct sink *)opaque;
  unsigned i;
  if (sink->reject) { sink->reject = 0; return 0; }
  for (i = 0; i < view->count; ++i) verify((const row *)view->data + i, sink->seen++);
  return 1;
}
static void publication(void) {
  xe_command_rows q; struct sink sink; unsigned i;
  memset(&q, 0, sizeof(q)); CHECK(xe_command_rows_init(&q, sizeof(row), 64));
  for (i = 0; i < 19; ++i) { row *r = (row *)xe_command_rows_append(&q); CHECK(r); fill(r, i); }
  sink.seen = 0; sink.reject = 1;
  CHECK(!xe_command_rows_submit(&q, 2, emit, &sink) && !sink.seen && q.pending == 19);
  CHECK(xe_command_rows_submit(&q, 2, emit, &sink) && sink.seen == 19 && !q.pending);
  CHECK(!xe_command_rows_submit(&q, 0, emit, &sink));
  CHECK(!xe_command_rows_submit(&q, 1, NULL, &sink));
  xe_command_rows_close(&q); CHECK(!live);
}
int main(void) { lifetime(); rollback_and_faults(); publication(); puts("command rows: direct identity, ordered spans, retries, rollback, reuse and ownership passed"); return 0; }
