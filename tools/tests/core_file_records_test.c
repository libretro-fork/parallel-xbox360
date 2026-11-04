/* Fixed-record ranges over a synthetic short-read VFS and native mapping. C89. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "core_file.h"
#include <streams/file_stream.h>
#define LENGTH ((1u << 20) + 27u)
#define START 13u
#define RECORD 41u
static size_t position, max_request, read_bytes;
static unsigned reads, seeks, closes;
static int fault;
static unsigned char pattern(size_t offset) { return (unsigned char)((offset * 37) ^ (offset >> 8)); }
static struct retro_vfs_file_handle *RETRO_CALLCONV open_file(const char *path, unsigned mode, unsigned hints) {
   (void)hints;
   if (strcmp(path, "records://cache") || mode != RETRO_VFS_FILE_ACCESS_READ) return NULL;
   position = 0; return (struct retro_vfs_file_handle *)&position;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle *file) {(void)file; ++closes; return 0;}
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle *file) {(void)file; return LENGTH;}
static int64_t RETRO_CALLCONV tell_file(struct retro_vfs_file_handle *file) {(void)file; return (int64_t)position;}
static int64_t RETRO_CALLCONV seek_file(struct retro_vfs_file_handle *file, int64_t offset, int origin) {
   (void)file; ++seeks;
   if (fault == 2 || origin != RETRO_VFS_SEEK_POSITION_START || offset < 0 || offset > LENGTH) return -1;
   position = (size_t)offset; return (int64_t)position;
}
static int64_t RETRO_CALLCONV read_file(struct retro_vfs_file_handle *file, void *out, uint64_t length) {
   size_t i, count = length > 8191 ? 8191 : (size_t)length;
   unsigned char *bytes = (unsigned char *)out;
   (void)file; ++reads;
   if (length > max_request) max_request = (size_t)length;
   if (fault == 1) return 0;
   if (count > LENGTH - position) count = LENGTH - position;
   for (i = 0; i < count; ++i) bytes[i] = pattern(position + i);
   position += count; read_bytes += count; return (int64_t)count;
}
static int record_matches(xe_file_records *records, size_t index) {
   unsigned char copy[RECORD + 2];
   const uint8_t *view;
   size_t i;
   memset(copy, 0xAD, sizeof(copy));
   view = xe_file_records_get(records, index);
   if (!view) return 0;
   for (i = 0; i < RECORD; ++i) if (view[i] != pattern(START + index * RECORD + i)) return 0;
   if (!xe_file_records_read(records, index, copy + 1, RECORD) || copy[0] != 0xAD || copy[RECORD + 1] != 0xAD) return 0;
   for (i = 0; i < RECORD; ++i) if (copy[i + 1] != pattern(START + index * RECORD + i)) return 0;
   return 1;
}
int main(int argc, char **argv) {
   struct retro_vfs_interface iface;
   struct retro_vfs_interface_info info;
   xe_file_records *records;
   xe_file *native;
   FILE *output;
   unsigned char block[4096], copy[RECORD];
   size_t i, count, offset, bytes;
   unsigned before;
   uintptr_t first, last;
   if (argc != 2) return 1;
   memset(&iface, 0, sizeof(iface));
   iface.open = open_file; iface.close = close_file; iface.size = size_file;
   iface.tell = tell_file; iface.seek = seek_file; iface.read = read_file;
   info.required_interface_version = FILESTREAM_REQUIRED_VFS_VERSION; info.iface = &iface;
   filestream_vfs_init(&info);
   records = xe_file_records_open("records://cache", START, RECORD);
   count = (LENGTH - START) / RECORD;
   if (!records || xe_file_records_count(records) != count) return 2;
   for (i = 0; i < count; ++i) if (!record_matches(records, i)) return 3;
   if (max_request > 65536 || max_request < 65536 - RECORD || reads > 256 ||
       read_bytes != count * RECORD || seeks != 1) return 4;
   before = reads;
   if (!record_matches(records, count - 1) || reads != before ||
       xe_file_records_get(records, count) || xe_file_records_get(records, SIZE_MAX) ||
       xe_file_records_read(records, 0, copy, RECORD - 1)) return 5;
   fault = 1; memset(copy, 0xAD, sizeof(copy));
   if (xe_file_records_get(records, 0) || xe_file_records_read(records, 0, copy, RECORD)) return 6;
   for (i = 0; i < RECORD; ++i) if (copy[i] != 0xAD) return 7;
   fault = 0; if (!record_matches(records, 0)) return 8;
   fault = 2; if (xe_file_records_get(records, count - 1)) return 9;
   fault = 0; if (!record_matches(records, count - 1)) return 10;
   xe_file_records_close(records);
   if (closes != 1 || xe_file_records_open("records://cache", 0, 0) ||
       xe_file_records_open("records://cache", UINT64_MAX, RECORD)) return 11;
   info.iface = NULL; filestream_vfs_init(&info);
   output = fopen(argv[1], "wb"); if (!output) return 12;
   for (offset = 0; offset < LENGTH; offset += bytes) {
      bytes = LENGTH - offset; if (bytes > sizeof(block)) bytes = sizeof(block);
      for (i = 0; i < bytes; ++i) block[i] = pattern(offset + i);
      if (fwrite(block, 1, bytes, output) != bytes) return 13;
   }
   if (fclose(output)) return 14;
   records = xe_file_records_open(argv[1], START, RECORD);
   native = xe_file_open(argv[1]);
   if (!records || !native || xe_file_records_count(records) != count) return 15;
   first = (uintptr_t)xe_file_records_get(records, 0);
   if (!record_matches(records, 0) || !record_matches(records, count - 1)) return 16;
   last = (uintptr_t)xe_file_records_get(records, count - 1);
   if (xe_file_view(native, 0, LENGTH) && last - first != (count - 1) * RECORD) return 17;
   xe_file_records_close(records); xe_file_close(native);
   records = xe_file_records_open(argv[1], LENGTH, RECORD);
   if (!records || xe_file_records_count(records) || xe_file_records_get(records, 0)) return 18;
   xe_file_records_close(records); remove(argv[1]);
   puts("PASS: mapped records, bounded VFS windows, no per-record seeks/reads, errors, trailing bytes and output bounds");
   return 0;
}
