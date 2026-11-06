#ifndef XE_TEST_CONTENT_FIXTURE_H
#define XE_TEST_CONTENT_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
typedef struct xe_test_content {
  const uint8_t *bytes;
  size_t size;
  int map, read_fault, open_fault, throw_read, throw_consumer;
  unsigned opened, destroyed, mapped, unmapped, consumed, reads;
  unsigned allocations, frees;
  uint8_t *owned;
  int invalid;
} xe_test_content;
#ifdef __cplusplus
extern "C" {
#endif
int xe_test_content_read(xe_test_content *, size_t, void *, size_t, size_t *);
void xe_test_content_consume(xe_test_content *, const uint8_t *, size_t);
int xe_test_content_bridge(xe_test_content *);
void *xe_test_buffer_allocate(size_t);
void xe_test_buffer_free(void *);
int xe_test_archive_bridge(const char *, int);
int xe_test_archive_bytes(const uint8_t *, size_t, int);
#ifdef __cplusplus
}
#endif
#endif
