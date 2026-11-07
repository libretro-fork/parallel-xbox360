/* Bounded package header I/O; no payload staging or filesystem stat. MSVC C89. */
#ifndef XE_CORE_PACKAGE_HEADER_H
#define XE_CORE_PACKAGE_HEADER_H
#include "core_file_probe.h"

static INLINE int xe_package_read_exact(RFILE *file, void *out, size_t length)
{
   size_t done = 0, request;
   int64_t got;
   if (!file || (length && !out)) return 0;
   while (done < length) {
      request = length - done;
      if ((uint64_t)request > INT64_MAX) request = (size_t)INT64_MAX;
      errno = 0;
      got = filestream_read(file, (unsigned char *)out + done, (int64_t)request);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0 || (uint64_t)got > request) return 0;
      done += (size_t)got;
   }
   return 1;
}
/* 1 complete, 0 too small, -1 I/O/argument failure. Destination is private until
 * success and may hold a prefix on error. No allocation; exactly one handle. */
static INLINE int xe_package_read_header(const char *path, void *out,
      size_t length, uint64_t *file_size)
{
   RFILE *file;
   int64_t size;
   int result = -1;
   if (!path || !*path || !out || !length) return -1;
   file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!file) return -1;
   size = filestream_get_size(file);
   if (size >= 0) {
      if ((uint64_t)size < length) result = 0;
      else if (xe_package_read_exact(file, out, length)) result = 1;
   }
   if (filestream_close(file) != 0) result = -1;
   if (result == 1 && file_size) *file_size = (uint64_t)size;
   return result;
}
/* Probe only the signature and big-endian volume type. Output is unchanged on
 * failure. 1 supported, 0 wrong/truncated package, -1 I/O/argument failure. */
static INLINE int xe_package_probe_type(const char *path, size_t minimum,
      size_t volume_offset, unsigned *volume)
{
   RFILE *file;
   int64_t size;
   unsigned char head[4], bytes[4];
   uint32_t value = 0;
   int result = 0;
   if (!path || !*path || !volume || minimum < 4 ||
       volume_offset > minimum - 4 || (uint64_t)volume_offset > INT64_MAX)
      return -1;
   file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!file) return -1;
   size = filestream_get_size(file);
   if (size < 0) result = -1;
   else if ((uint64_t)size >= minimum) {
      if (!xe_file_probe_four(file, head)) result = -1;
      else if (!memcmp(head, "CON ", 4) || !memcmp(head, "LIVE", 4) ||
               !memcmp(head, "PIRS", 4)) {
         if (filestream_seek(file, (int64_t)volume_offset,
                  RETRO_VFS_SEEK_POSITION_START) != (int64_t)volume_offset ||
             !xe_file_probe_four(file, bytes)) result = -1;
         else {
            value = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                  ((uint32_t)bytes[2] << 8) | bytes[3];
            if (value <= 1) result = 1;
         }
      }
   }
   if (filestream_close(file) != 0) result = -1;
   if (result == 1) *volume = (unsigned)value;
   return result;
}
#endif
