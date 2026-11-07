/* Production common-stream ownership, short reads and mapping; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <streams/file_stream.h>
#include "core_file.h"
static unsigned char data[257];
static size_t position;
static unsigned reads, seeks, closes;
static int fault;
static struct retro_vfs_file_handle *RETRO_CALLCONV open_file(
    const char *path, unsigned mode, unsigned hints) {
  (void)hints;
  if (strcmp(path, "content://archive") || mode != RETRO_VFS_FILE_ACCESS_READ)
    return NULL;
  position = 0;
  return (struct retro_vfs_file_handle *)data;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle *file) {
  (void)file; ++closes; return 0;
}
static int64_t RETRO_CALLCONV file_size(struct retro_vfs_file_handle *file) {
  (void)file; return sizeof(data);
}
static int64_t RETRO_CALLCONV file_tell(struct retro_vfs_file_handle *file) {
  (void)file; return (int64_t)position;
}
static int64_t RETRO_CALLCONV file_seek(struct retro_vfs_file_handle *file,
    int64_t offset, int origin) {
  (void)file; ++seeks;
  if (origin != RETRO_VFS_SEEK_POSITION_START || offset < 0 ||
      (uint64_t)offset > sizeof(data) || fault == 3) return -1;
  if (fault == 5) return offset + 1;
  position = (size_t)offset; return 0;
}
static int64_t RETRO_CALLCONV file_read(struct retro_vfs_file_handle *file,
    void *out, uint64_t length) {
  size_t count;
  (void)file; ++reads;
  if (fault == 4) { fault = 0; errno = EINTR; return -1; }
  if (fault == 1) return -1;
  if (fault == 2) return 0;
  count = length > 7 ? 7 : (size_t)length;
  if (count > sizeof(data) - position) count = sizeof(data) - position;
  memcpy(out, data + position, count); position += count;
  return (int64_t)count;
}
int main(int argc, char **argv) {
  struct retro_vfs_interface iface;
  struct retro_vfs_interface_info info;
  xe_file *file;
  xe_file_blob blob;
  xe_file_ranges *ranges;
  const uint8_t *first;
  FILE *output;
  const void *view;
  unsigned char buffer[259];
  unsigned i, before;
  size_t transferred;
  if (argc != 2) return 1;
  for (i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)(i * 37);
  memset(&iface, 0, sizeof(iface));
  iface.open = open_file; iface.close = close_file; iface.size = file_size;
  iface.tell = file_tell; iface.seek = file_seek; iface.read = file_read;
  info.required_interface_version = 2; info.iface = &iface;
  filestream_vfs_init(&info);
  file = xe_file_open("content://archive");
  if (!file || xe_file_size(file) != sizeof(data) || xe_file_view(file, 0, 1)) return 2;
  reads = seeks = 0;
  memset(buffer, 0xAD, sizeof(buffer));
  if (!xe_file_read(file, 0, buffer + 1, 100) || reads != 15 || seeks != 1 ||
      memcmp(buffer + 1, data, 100) || buffer[0] != 0xAD || buffer[101] != 0xAD) return 3;
  if (!xe_file_read(file, 100, buffer, 3) || seeks != 1 || memcmp(buffer, data + 100, 3)) return 4;
  before = reads;
  if (xe_file_read(file, 257, buffer, 1) || xe_file_read(file, (uint64_t)-1, buffer, 1) ||
      xe_file_read(file, 0, NULL, 1) || !xe_file_read(file, 257, NULL, 0) || reads != before) return 5;
  for (fault = 1; fault <= 3; ++fault) {
    if (xe_file_read(file, 103, buffer, 3)) return 6;
  }
  fault = 0;
  if (!xe_file_read(file, 103, buffer, 3) || memcmp(buffer, data + 103, 3)) return 7;
  fault = 4;
  if (!xe_file_read(file, 106, buffer, 3) || memcmp(buffer, data + 106, 3)) return 14;
  fault = 5; before = reads; transferred = 99;
  memset(buffer, 0xAD, sizeof(buffer));
  if (xe_file_read_transfer(file, 5, buffer, 3, &transferred) || transferred ||
      reads != before || buffer[0] != 0xAD) return 30;
  fault = 0;
  if (!xe_file_read_transfer(file, 5, buffer, 3, &transferred) || transferred != 3 ||
      memcmp(buffer, data + 5, 3)) return 31;
  xe_file_close(file);
  if (closes != 1) return 8;
  memset(&blob, 0, sizeof(blob));
  if (!xe_file_blob_open(&blob, "content://archive") || blob.size != sizeof(data) ||
      !blob.storage || memcmp(blob.data, data, sizeof(data))) return 16;
  if (xe_file_blob_open(&blob, "content://archive")) return 17;
  xe_file_blob_close(&blob); xe_file_blob_close(&blob);
  if (blob.data || blob.owner || blob.storage || blob.size) return 18;
  for (fault = 1; fault <= 2; ++fault) {
    if (xe_file_blob_open(&blob, "content://archive") ||
        blob.data || blob.owner || blob.storage || blob.size) return 19;
  }
  fault = 0;
  ranges = xe_file_ranges_open("content://archive");
  if (!ranges || xe_file_ranges_size(ranges) != sizeof(data)) return 24;
  first = xe_file_ranges_get(ranges, 1, 100);
  if (!first || memcmp(first, data + 1, 100)) return 25;
  view = xe_file_ranges_get(ranges, 7, 80);
  if (view != first || memcmp(view, data + 7, 80)) return 26;
  before = reads;
  if (!xe_file_ranges_get(ranges, sizeof(data), 0) ||
      xe_file_ranges_get(ranges, UINT64_MAX, 0) ||
      xe_file_ranges_get(ranges, 250, 8) || reads != before) return 27;
  fault = 1;
  if (xe_file_ranges_get(ranges, 100, 100)) return 28;
  fault = 0;
  if (!xe_file_ranges_get(ranges, 100, 100)) return 29;
  xe_file_ranges_close(ranges);
  info.iface = NULL; filestream_vfs_init(&info);
  output = fopen(argv[1], "wb");
  if (!output) return 9;
  if (fwrite(data, 1, sizeof(data), output) != sizeof(data) || fclose(output)) return 10;
  file = xe_file_open(argv[1]);
  if (!file || xe_file_size(file) != sizeof(data)) return 11;
  view = xe_file_view(file, 7, 200);
  if (view && memcmp(view, data + 7, 200)) return 12;
  if (!xe_file_read(file, 0, buffer + 1, sizeof(data)) || memcmp(buffer + 1, data, sizeof(data))) return 13;
  if (!xe_file_read(file, 200, buffer, 31) || memcmp(buffer, data + 200, 31) ||
      !xe_file_read(file, 7, buffer, 3) || memcmp(buffer, data + 7, 3)) return 15;
  printf("PASS: VFS short reads, seek elision, errors, bounds and native %s\n", view ? "mapping" : "fallback");
  xe_file_close(file);
  if (!xe_file_blob_open(&blob, argv[1]) || blob.size != sizeof(data) ||
      memcmp(blob.data, data, sizeof(data))) return 20;
  if (xe_file_view(blob.owner, 0, blob.size) && blob.storage) return 21;
  xe_file_blob_close(&blob);
  ranges = xe_file_ranges_open(argv[1]);
  if (!ranges) return 30;
  first = xe_file_ranges_get(ranges, 7, 100);
  if (!first || memcmp(first, data + 7, 100)) return 31;
  view = xe_file_ranges_get(ranges, 8, 99);
  if (!view || memcmp(view, data + 8, 99)) return 32;
  if (first && view && view != first && view != first + 1) return 33;
  xe_file_ranges_close(ranges);
  output = fopen(argv[1], "wb"); if (!output || fclose(output)) return 22;
  if (!xe_file_blob_open(&blob, argv[1]) || blob.data || blob.storage || blob.size) return 23;
  xe_file_blob_close(&blob);
  remove(argv[1]);
  return 0;
}
