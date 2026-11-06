/* Parser-input ownership, allocation and transfer failures; MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations, releases;
static int allocation_failure;
static size_t allocation_size;
static unsigned char *allocation;
static void *test_allocate(size_t size) {
  ++allocations; allocation_size = size;
  if (allocation_failure) return NULL;
  allocation = (unsigned char *)malloc(size);
  if (allocation) memset(allocation, 0xcd, size);
  return allocation;
}
static void test_free(void *pointer) {
  if (pointer) { ++releases; allocation = NULL; }
  free(pointer);
}
#define XE_CONTENT_ALLOC test_allocate
#define XE_CONTENT_FREE test_free
#include "core_content_input.h"
enum { SIZE = 2097169 };
static unsigned char payload[SIZE + 64];
static int fault, map, invalid;
static unsigned reads, views;
static size_t limit;
#ifdef XE_TEST_CONTENT_BRIDGE
#include "core_content_fixture.h"
#include <streams/file_stream.h>
static xe_test_content *active;
void *xe_test_buffer_allocate(size_t size) {
  ++active->allocations;
  if (size != active->size) active->invalid = 1;
  active->owned = (uint8_t *)malloc(size);
  if (active->owned) memset(active->owned, 0xcd, size);
  return active->owned;
}
void xe_test_buffer_free(void *data) {
  if (data) {
    if (data != active->owned) active->invalid = 1;
    ++active->frees; active->owned = NULL;
  }
  free(data);
}
int xe_test_content_read(xe_test_content *test, size_t offset, void *out,
    size_t wanted, size_t *received) {
  size_t n = wanted > 4093 ? 4093 : wanted;
  ++test->reads;
  if (offset > test->size || wanted > test->size - offset ||
      out != test->owned + offset || ((uint8_t *)out)[0] != 0xcd)
    test->invalid = 1;
  if (test->read_fault == 1) return 0;
  if (test->read_fault == 2 || (test->read_fault == 4 && offset)) {
    *received = 0; return 1;
  }
  if (test->read_fault == 3) { *received = wanted + 1; return 1; }
  memcpy(out, test->bytes + offset, n); *received = n; return 1;
}
void xe_test_content_consume(xe_test_content *test, const uint8_t *data, size_t size) {
  ++test->consumed;
  if (size != test->size || memcmp(data, test->bytes, size)) test->invalid = 1;
  if (test->map == 1) {
    if (data != test->bytes || test->opened || test->unmapped) test->invalid = 1;
  } else if (data != test->owned || test->destroyed) test->invalid = 1;
}
int xe_test_archive_bytes(const uint8_t *data, size_t size, int compressed) {
  size_t i;
  if (size != 131072) return 0;
  for (i = 0; i < size; ++i)
    if (data[i] != (uint8_t)(compressed ? (i / 65536) * 37 + 5 : i * 37)) return 0;
  return 1;
}
static void put32(uint8_t *data, size_t offset, uint32_t value) {
  unsigned i;
  for (i = 0; i < 4; ++i) data[offset + i] = (uint8_t)(value >> (24 - i * 8));
}
static void put64(uint8_t *data, size_t offset, uint64_t value) {
  put32(data, offset, (uint32_t)(value >> 32)); put32(data, offset + 4, (uint32_t)value);
}
static int make_archive(const char *path, int compressed) {
  static uint8_t image[131292];
  static const uint8_t frame[11] = {0x28,0xb5,0x2f,0xfd,0x60,0,0xff,3,0,8,5};
  size_t payload_size = compressed ? 22 : 131072;
  size_t names = payload_size + 40, tree = names + 4, footer = tree + 32;
  size_t total = footer + 144, i;
  RFILE *file;
  int ok;
  memset(image, 0, sizeof(image));
  if (compressed) {
    memcpy(image, frame, 11); memcpy(image + 11, frame, 11); image[21] = 42;
  } else for (i = 0; i < payload_size; ++i) image[i] = (uint8_t)(i * 37);
  for (i = 0; i < 16; ++i) {
    image[payload_size + 8 + i * 2] = compressed ? 0 : 255;
    image[payload_size + 9 + i * 2] = compressed ? 10 : 255;
  }
  memcpy(image + names, "\003raw", 4);
  put32(image, tree, 0x7fffffffU); put32(image, tree + 4, 1); put32(image, tree + 8, 1);
  put32(image, tree + 16, 0x80000000U); put32(image, tree + 24, 131072);
  put64(image, footer + 8, payload_size);
  put64(image, footer + 16, payload_size); put64(image, footer + 24, 40);
  put64(image, footer + 32, names); put64(image, footer + 40, 4);
  put64(image, footer + 48, tree); put64(image, footer + 56, 32);
  put64(image, footer + 128, total); put32(image, footer + 136, 0x61bf3a01U);
  put32(image, footer + 140, 0x169f52d6U);
  file = filestream_open(path, RETRO_VFS_FILE_ACCESS_WRITE, 0);
  if (!file) return 0;
  ok = filestream_write(file, image, (int64_t)total) == (int64_t)total;
  if (filestream_close(file)) ok = 0;
  return ok;
}
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static const uint8_t *view(void *opaque, uint64_t *available) {
  (void)opaque; ++views;
  *available = map == 2 ? 3 : sizeof(payload);
  return map ? payload : NULL;
}
static int read_input(void *opaque, uint64_t offset, void *data,
    size_t wanted, size_t *received) {
  size_t n = wanted;
  (void)opaque; ++reads;
  if (!allocation || data != allocation + (size_t)offset || wanted > 1048576 ||
      offset >= SIZE || wanted > SIZE - offset ||
      ((unsigned char *)data)[0] != 0xcd) invalid = 1;
  if (fault == 1) return 0;
  if (fault == 2 || (fault == 4 && offset)) { *received = 0; return 1; }
  if (fault == 3) { *received = wanted + 1; return 1; }
  if (n > limit) n = limit;
  memcpy(data, payload + (size_t)offset, n); *received = n;
  return 1;
}
static void reset(int failure, int mapped) {
  allocations = releases = reads = views = 0;
  fault = failure; map = mapped; invalid = allocation_failure = 0;
  allocation_size = 0; limit = 1048576;
}
int main(int argc, char **argv) {
  xe_content_source source;
  xe_content_input input;
  size_t i;
  int mode, failure;
  (void)argc; (void)argv;
  memset(&input, 0, sizeof(input));
  source.opaque = NULL; source.size = SIZE;
  source.view = view; source.read = read_input;
  for (i = 0; i < sizeof(payload); ++i) payload[i] = (unsigned char)(i * 29 + i / 251);
  for (mode = 0; mode < 3; ++mode) {
    reset(0, mode);
    CHECK(xe_content_input_acquire(&input, &source, 4) == 1);
    CHECK(!invalid && input.size == SIZE && !memcmp(input.data, payload, SIZE));
    CHECK(mode == 1 ? input.data == payload && !allocations && !reads :
        input.data == allocation && allocations == 1 && reads == 3 && allocation_size == SIZE);
    CHECK(!xe_content_input_acquire(&input, &source, 4));
    xe_content_input_release(&input);
    CHECK(!input.data && !input.size && !input.allocation);
    CHECK(releases == allocations);
    xe_content_input_release(&input); CHECK(releases == allocations);
  }
  reset(0, 0); limit = 4093;
  CHECK(xe_content_input_acquire(&input, &source, 4) == 1);
  CHECK(!invalid && reads > 3 && !memcmp(input.data, payload, SIZE));
  xe_content_input_release(&input); CHECK(releases == 1);
  for (failure = 1; failure <= 4; ++failure) {
    reset(failure, 0);
    CHECK(!xe_content_input_acquire(&input, &source, 4));
    CHECK(!invalid && allocations == 1 && releases == 1 && !input.data && !input.allocation);
  }
  reset(0, 0); allocation_failure = 1;
  CHECK(xe_content_input_acquire(&input, &source, 4) == -1);
  CHECK(allocations == 1 && !releases && !reads && !input.data);
  reset(0, 1); allocation_failure = 1;
  CHECK(xe_content_input_acquire(&input, &source, 4) == 1);
  CHECK(input.data == payload && !allocations && !reads);
  xe_content_input_release(&input);
  reset(0, 0); source.read = NULL;
  CHECK(!xe_content_input_acquire(&input, &source, 4)); CHECK(!allocations);
  reset(0, 0); source.read = read_input; source.view = NULL;
  CHECK(xe_content_input_acquire(&input, &source, 4) == 1);
  CHECK(!invalid && !views && allocations == 1);
  xe_content_input_release(&input);
  reset(0, 1); source.view = view;
  for (i = 0; i < 4; ++i) {
    source.size = i; CHECK(!xe_content_input_acquire(&input, &source, 4));
  }
  CHECK(!allocations && !views && !reads);
  CHECK(!xe_content_input_acquire(&input, NULL, 4));
  CHECK(!xe_content_input_acquire(NULL, &source, 4));
#ifdef XE_TEST_CONTENT_BRIDGE
  {
    xe_test_content test;
    int result;
    for (mode = 0; mode < 4; ++mode) {
      for (failure = 0; failure < 10; ++failure) {
        memset(&test, 0, sizeof(test));
        test.bytes = payload; test.size = SIZE; test.map = mode;
        if (failure < 5) test.read_fault = failure;
        else if (failure < 8) test.open_fault = failure - 5;
        else if (failure == 8) test.throw_read = 1;
        else test.throw_consumer = 1;
        active = &test;
        result = xe_test_content_bridge(&test);
        CHECK(!test.invalid && !test.owned && test.allocations == test.frees);
        CHECK(test.mapped == test.unmapped);
        CHECK(test.destroyed == (test.opened && test.open_fault != 1 ? 1u : 0u));
        if (mode == 1) {
          CHECK(!test.allocations && !test.opened && test.consumed == 1);
          CHECK(result == (failure == 9 ? 2 : 1));
        } else if (test.read_fault || test.open_fault) CHECK(result <= 0 && !test.consumed);
        else if (test.throw_read || test.throw_consumer) CHECK(result == 2);
        else CHECK(result == 1 && test.consumed == 1);
      }
    }
    active = NULL;
    puts("Actual guest Entry/File bridge: failure, partial mapping, parser/read exceptions and owner retirement pass");
    CHECK(argc == 2);
    for (mode = 0; mode < 2; ++mode) {
      memset(&test, 0, sizeof(test)); test.size = 131072; active = &test;
      CHECK(make_archive(argv[1], mode));
      CHECK(xe_test_archive_bridge(argv[1], mode));
      CHECK(!test.invalid && !test.owned && test.allocations == (unsigned)mode && test.frees == test.allocations);
      CHECK(!filestream_delete(argv[1]));
    }
    active = NULL;
    puts("Actual ZArchive entries: raw mapped borrow, compressed fallback, slices and readonly bounds pass");
  }
#endif
  puts("Content input: direct borrowed views, exact uninitialized storage, partial reads and failure cleanup pass");
  return 0;
}
