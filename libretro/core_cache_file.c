/* Persistent cache I/O through owned common VFS; MSVC C89. */
#include "core_cache_file.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <encodings/xxh3.h>

RFILE *xe_cache_file_open(const char *path) {
   RFILE *file;
   if (!path || !*path) return NULL;
   file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ_WRITE |
         RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
   if (!file && !filestream_exists(path))
      file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ_WRITE, 0);
   return file;
}

size_t xe_cache_file_read(void *out, size_t size, size_t count, RFILE *file) {
   size_t total, done = 0, request;
   int64_t got;
   unsigned char *destination = (unsigned char *)out;
   if (!file || !size || !count || !out || count > SIZE_MAX / size) return 0;
   total = count * size;
   while (done < total) {
      request = total - done;
      if ((uint64_t)request > INT64_MAX) request = (size_t)INT64_MAX;
      errno = 0;
      got = filestream_read(file, destination + done, (int64_t)request);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0 || (uint64_t)got > request) break;
      done += (size_t)got;
   }
   return done / size;
}

size_t xe_cache_file_write(const void *data, size_t size, size_t count, RFILE *file) {
   size_t total, done = 0, request;
   int64_t got;
   const unsigned char *source = (const unsigned char *)data;
   if (!file || !size || !count || !data || count > SIZE_MAX / size) return 0;
   total = count * size;
   while (done < total) {
      request = total - done;
      if ((uint64_t)request > INT64_MAX) request = (size_t)INT64_MAX;
      errno = 0;
      got = filestream_write(file, source + done, (int64_t)request);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0 || (uint64_t)got > request) break;
      done += (size_t)got;
   }
   return done / size;
}

int xe_cache_file_write_be32(RFILE *file, const uint32_t *words, size_t count) {
   unsigned char bytes[4096];
   size_t i, chunk;
   uint32_t value;
   if (!file || (!words && count) || count > SIZE_MAX / sizeof(uint32_t)) return 0;
   while (count) {
      chunk = count > sizeof(bytes) / 4 ? sizeof(bytes) / 4 : count;
      for (i = 0; i < chunk; ++i) {
         value = words[i];
         bytes[i * 4] = (unsigned char)(value >> 24);
         bytes[i * 4 + 1] = (unsigned char)(value >> 16);
         bytes[i * 4 + 2] = (unsigned char)(value >> 8);
         bytes[i * 4 + 3] = (unsigned char)value;
      }
      if (xe_cache_file_write(bytes, 4, chunk, file) != chunk) return 0;
      words += chunk; count -= chunk;
   }
   return 1;
}

int xe_cache_file_has_bytes(RFILE *file, uint64_t length) {
   int64_t size, position;
   if (!file) return 0;
   size = filestream_get_size(file);
   position = filestream_tell(file);
   return size >= 0 && position >= 0 && position <= size &&
         length <= (uint64_t)(size - position);
}

int xe_cache_file_repair(RFILE *file, uint64_t length) {
   if (!file || length > INT64_MAX || filestream_flush(file) != 0 ||
         filestream_truncate(file, (int64_t)length) < 0) return 0;
   return filestream_seek(file, (int64_t)length,
         RETRO_VFS_SEEK_POSITION_START) >= 0;
}

int xe_cache_records_validate(xe_file_records *records, size_t record_size,
      size_t hash_offset, size_t payload_offset, size_t payload_size,
      xe_cache_record_sink sink, void *opaque, size_t *valid_count) {
   size_t index, count;
   uint64_t expected;
   const uint8_t *row;
   if (valid_count) *valid_count = 0;
   if (!records || !valid_count || record_size != xe_file_records_stride(records) || hash_offset > record_size ||
         sizeof(expected) > record_size - hash_offset || payload_offset > record_size ||
         payload_size > record_size - payload_offset) return 0;
   count = xe_file_records_count(records);
   for (index = 0; index < count; ++index) {
      row = xe_file_records_get(records, index);
      if (!row) return 0;
      memcpy(&expected, row + hash_offset, sizeof(expected));
      if (xxh3_64(row + payload_offset, payload_size, 0) != expected) return 1;
      if (sink && !sink(opaque, row, record_size)) return 0;
      ++*valid_count;
   }
   return 1;
}
