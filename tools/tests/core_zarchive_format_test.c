/* Independent byte-format oracle, cache failure publication and bounds; C89. */
#include <stdio.h>
#include <string.h>
#include <streams/file_stream.h>
#include <rthreads/rthreads.h>
#include "core_zarchive.h"
#define COMPRESSED_BLOCKS 8320U
static unsigned char image[160000], output[703];
typedef char checksum_fixture_fits[(COMPRESSED_BLOCKS * 15 +
    ((COMPRESSED_BLOCKS + 15) / 16) * 40 + 11 + 64 + 144 <= sizeof(image)) ? 1 : -1];
static size_t image_size, cursor, names, tree, footer;
static uint64_t fail_at = (uint64_t)-1;
typedef struct reader_thread {
  xe_zar *reader;
  unsigned seed;
  unsigned blocks;
  int compressed;
  int failed;
} reader_thread;
static void concurrent_read(void *opaque) {
  reader_thread *state = (reader_thread *)opaque;
  unsigned rng = state->seed, i, j, block;
  unsigned char local[103], expected;
  for (i = 0; i < 2000; ++i) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    block = rng % state->blocks;
    memset(local, 0xa7, sizeof(local));
    if (xe_zar_read(state->reader, 1, (uint64_t)block * 65536 + 19, 101, local + 1) != 101 ||
        local[0] != 0xa7 || local[102] != 0xa7) { state->failed = 1; return; }
    for (j = 1; j <= 101; ++j) {
      expected = state->compressed ? (unsigned char)(block * 37 + 5) : (unsigned char)((19 + j - 1) * 37);
      if (local[j] != expected) { state->failed = 1; return; }
    }
  }
}
static void put32(size_t offset, uint32_t value) {
  image[offset] = (unsigned char)(value >> 24);
  image[offset + 1] = (unsigned char)(value >> 16);
  image[offset + 2] = (unsigned char)(value >> 8);
  image[offset + 3] = (unsigned char)value;
}
static void put64(size_t offset, uint64_t value) {
  put32(offset, (uint32_t)(value >> 32)); put32(offset + 4, (uint32_t)value);
}
static void make_image(int compressed) {
  static const unsigned char frame[11] = {0x28,0xb5,0x2f,0xfd,0x60,0,0xff,3,0,8,0};
  unsigned blocks = compressed ? COMPRESSED_BLOCKS : 2, records = (blocks + 15) / 16;
  size_t payload, offsets, i, j, frame_size = compressed == 2 ? 15 : 11;
  memset(image, 0, sizeof(image));
  payload = compressed ? blocks * frame_size : blocks * 65536U;
  if (compressed) {
    for (i = 0; i < blocks; ++i) {
      memcpy(image + i * frame_size, frame, sizeof(frame));
      image[i * frame_size + 10] = (unsigned char)(i * 37 + 5);
      if (compressed == 2) image[i * frame_size + 4] = 0x64;
    }
  } else for (i = 0; i < payload; ++i) image[i] = (unsigned char)(i * 37);
  offsets = payload;
  for (i = 0; i < records; ++i) {
    put64(offsets + i * 40, i * 16 * (compressed ? frame_size : 65536U));
    for (j = 0; j < 16; ++j) {
      image[offsets + i * 40 + 8 + j * 2] = compressed ? 0 : 255;
      image[offsets + i * 40 + 9 + j * 2] = compressed ? (unsigned char)(frame_size - 1) : 255;
    }
  }
  names = offsets + records * 40;
  memcpy(image + names, "\003raw\001d\004tail", 11);
  tree = names + 11; footer = tree + 64; image_size = footer + 144;
  put32(tree, 0x7fffffffU); put32(tree + 4, 1); put32(tree + 8, 2);
  put32(tree + 16, 0x80000000U); put32(tree + 24, blocks * 65536U);
  put32(tree + 32, 4); put32(tree + 36, 3); put32(tree + 40, 1);
  put32(tree + 48, 0x80000006U); put32(tree + 52, 65539); put32(tree + 56, 11);
  put64(footer, 0); put64(footer + 8, payload);
  put64(footer + 16, offsets); put64(footer + 24, records * 40);
  put64(footer + 32, names); put64(footer + 40, 11);
  put64(footer + 48, tree); put64(footer + 56, 64);
  put64(footer + 128, image_size); put32(footer + 136, 0x61bf3a01U);
  put32(footer + 140, 0x169f52d6U);
}
static struct retro_vfs_file_handle *RETRO_CALLCONV memory_open(const char *path, unsigned mode, unsigned hints) {
  (void)hints;
  if (strcmp(path, "memory://test") || mode != RETRO_VFS_FILE_ACCESS_READ) return NULL;
  cursor = 0; return (struct retro_vfs_file_handle *)image;
}
static int RETRO_CALLCONV memory_close(struct retro_vfs_file_handle *file) { (void)file; return 0; }
static int64_t RETRO_CALLCONV memory_size(struct retro_vfs_file_handle *file) { (void)file; return (int64_t)image_size; }
static int64_t RETRO_CALLCONV memory_seek(struct retro_vfs_file_handle *file, int64_t offset, int origin) {
  (void)file;
  if (origin != RETRO_VFS_SEEK_POSITION_START || offset < 0 || (uint64_t)offset > image_size) return -1;
  cursor = (size_t)offset; return 0;
}
static int64_t RETRO_CALLCONV memory_read(struct retro_vfs_file_handle *file, void *data, uint64_t length) {
  size_t count;
  (void)file;
  if (cursor == fail_at) return -1;
  count = length > 17 ? 17 : (size_t)length;
  if (count > image_size - cursor) count = image_size - cursor;
  memcpy(data, image + cursor, count); cursor += count; return (int64_t)count;
}
static xe_zar *open_image(const char *path) {
  FILE *file;
  if (strcmp(path, "memory://test")) {
    file = fopen(path, "wb");
    if (!file) return NULL;
    if (fwrite(image, 1, image_size, file) != image_size || fclose(file)) return NULL;
  }
  return xe_zar_open(path);
}
static int check_read(xe_zar *r, uint64_t offset, size_t length, int compressed) {
  size_t i;
  unsigned char expected;
  memset(output, 0xa7, sizeof(output));
  if (xe_zar_read(r, 1, offset, length, output + 1) != length) return 0;
  if (output[0] != 0xa7 || output[length + 1] != 0xa7) return 0;
  for (i = 0; i < length; ++i) {
    expected = compressed ? (unsigned char)(((offset + i) / 65536) * 37 + 5) : (unsigned char)((offset + i) * 37);
    if (output[i + 1] != expected) return 0;
  }
  return 1;
}
static int valid_test(const char *path, int compressed) {
  xe_zar *r;
  xe_zar_entry entry;
  unsigned i, block;
  size_t metadata;
  const uint8_t *view;
  reader_thread states[4];
  sthread_t *threads[4];
  make_image(compressed); r = open_image(path);
  if (!r) return 0;
  metadata = xe_zar_test_metadata_bytes(r);
  view = xe_zar_view(r, 1, 65400, 700);
  if ((view != NULL) != (!compressed && !metadata)) return 0;
  if (view) {
    if (!check_read(r, 65400, 700, compressed) || memcmp(view, output + 1, 700)) return 0;
    if (xe_zar_test_cache_bytes(r)) return 0;
  }
  if (xe_zar_view(r, 0, 0, 1) || xe_zar_view(r, 99, 0, 1) ||
      xe_zar_view(r, 1, (uint64_t)-1, 1) || xe_zar_view(r, 1, 0, 0) ||
      xe_zar_view(r, 1, xe_zar_size(r, 1), 1)) return 0;
  if (xe_zar_test_cache_bytes(r) || xe_zar_lookup(r, "//D\\tail", 8, 1, 0) != 3 ||
      xe_zar_lookup(r, "raw", 3, 0, 1) != XE_ZAR_INVALID ||
      xe_zar_lookup(r, NULL, 0, 0, 1) != 0 || xe_zar_type(r, 99) != -1 ||
      !xe_zar_entry_get(r, 0, 1, &entry) || entry.is_file || entry.name_size != 1 || entry.name[0] != 'd') return 0;
  if (!check_read(r, 65400, 700, compressed) || !check_read(r, 0, 700, compressed)) return 0;
  if (xe_zar_read(r, 1, 0, 1, NULL) || xe_zar_read(r, 1, 0, 0, NULL) ||
      xe_zar_read(r, 1, (uint64_t)-1, 700, output) || xe_zar_entry_get(r, 0, 2, &entry)) return 0;
  if (xe_zar_test_cache_bytes(r) != ((compressed || metadata) ? 4U * 1024 * 1024 : 0)) return 0;
  if (compressed) {
    /* Repeated colliding keys across every hash bucket, eviction and revisits. */
    for (i = 0; i < 4000; ++i) {
      block = (i * 73 + (i / 256) * 128) % COMPRESSED_BLOCKS;
      if (!check_read(r, (uint64_t)block * 65536 + 19, 37, 1)) return 0;
    }
    for (i = 0; i < 256; ++i)
      if (!check_read(r, (uint64_t)(i % 65) * 128 * 65536, 37, 1)) return 0;
  }
    for (i = 0; i < 4; ++i) {
      states[i].reader = r; states[i].seed = 101 + i; states[i].failed = 0;
      states[i].compressed = compressed; states[i].blocks = compressed ? COMPRESSED_BLOCKS : 2;
      threads[i] = sthread_create(concurrent_read, &states[i]);
      if (!threads[i]) {
        while (i) sthread_join(threads[--i]);
        xe_zar_close(r); return 0;
      }
    }
    for (i = 0; i < 4; ++i) sthread_join(threads[i]);
    for (i = 0; i < 4; ++i) if (states[i].failed) return 0;
  xe_zar_close(r); return 1;
}
static int malformed_test(const char *path) {
  xe_zar *r;
  unsigned mutation;
  for (mutation = 0; mutation < 13; ++mutation) {
    make_image(0);
    switch (mutation) {
      case 0: put64(footer + 32, (uint64_t)-3); put64(footer + 40, 8); break;
      case 1: put32(tree + 8, 0xffffffffU); break;
      case 2: put32(tree + 36, 1); break;
      case 3: put32(tree + 8, 1); put32(tree + 36, 3); put32(tree + 48, 6); put32(tree + 52, 2); put32(tree + 56, 1); break;
      case 4: put32(tree + 16, 0x8000000bU); break;
      case 5: image[names + 10] = 0x80; put32(tree + 16, 0x8000000aU); break;
      case 6: image[names + 1] = 0; break;
      case 7: image[names + 1] = '/'; break;
      case 8: put64(footer + 24, 39); break;
      case 9: put64(footer + 56, 63); break;
      case 10: put32(tree + 28, 0xffffffffU); break;
      case 11: put32(tree + 4, 0); break;
      case 12: image[footer + 143] ^= 1; break;
    }
    r = open_image(path);
    if (r) { xe_zar_close(r); return 0; }
  }
  make_image(0); put64(2 * 65536U, (uint64_t)-3); r = open_image(path);
  if (!r || xe_zar_read(r, 1, 0, 17, output)) return 0;
  xe_zar_close(r);
  /* A valid RLE block decodes privately before its bad checksum rejects it. */
  make_image(2); r = open_image(path); if (!r) return 0;
  memset(output, 0xa7, sizeof(output));
  if (xe_zar_read(r, 1, 0, 17, output + 1) || xe_zar_read(r, 1, 0, 17, output + 1)) return 0;
  for (mutation = 0; mutation < sizeof(output); ++mutation) if (output[mutation] != 0xa7) return 0;
  xe_zar_close(r);
  make_image(1); image[64 * 11] = 0; r = open_image(path);
  if (!r) return 0;
  for (mutation = 0; mutation < 64; ++mutation)
    if (!check_read(r, (uint64_t)mutation * 65536, 17, 1)) return 0;
  memset(output, 0xa7, sizeof(output));
  if (xe_zar_read(r, 1, (uint64_t)64 * 65536, 17, output + 1)) return 0;
  for (mutation = 0; mutation < sizeof(output); ++mutation) if (output[mutation] != 0xa7) return 0;
  if (!check_read(r, 0, 17, 1) || xe_zar_read(r, 1, (uint64_t)64 * 65536, 17, output)) return 0;
  xe_zar_close(r);
  if (!strcmp(path, "memory://test")) {
    make_image(1); r = open_image(path); if (!r) return 0;
    fail_at = 64 * 11;
    if (xe_zar_read(r, 1, (uint64_t)64 * 65536, 17, output)) return 0;
    fail_at = (uint64_t)-1;
    if (!check_read(r, (uint64_t)64 * 65536, 17, 1)) return 0;
    xe_zar_close(r);
  }
  return 1;
}
int main(int argc, char **argv) {
  struct retro_vfs_interface iface;
  struct retro_vfs_interface_info info;
  unsigned mode;
  const char *path;
  if (argc == 3) {
    xe_zar *fixture;
    make_image(strcmp(argv[2], "raw") != 0);
    fixture = open_image(argv[1]);
    if (!fixture) return 1;
    xe_zar_close(fixture); return 0;
  }
  if (argc != 2) return 1;
  memset(&iface, 0, sizeof(iface));
  iface.open = memory_open; iface.close = memory_close; iface.size = memory_size;
  iface.seek = memory_seek; iface.read = memory_read;
  info.required_interface_version = 2;
  for (mode = 0; mode < 2; ++mode) {
    info.iface = mode ? &iface : NULL; filestream_vfs_init(&info);
    path = mode ? "memory://test" : argv[1];
    if (!valid_test(path, 0)) return 2 + (int)mode;
    if (!valid_test(path, 1)) return 4 + (int)mode;
    if (!malformed_test(path)) return 6 + (int)mode;
  }
  remove(argv[1]);
  puts("PASS: independent raw/RLE format, borrowed storage, hash churn, graph/bounds rejection and failed-cache publication");
  return 0;
}
