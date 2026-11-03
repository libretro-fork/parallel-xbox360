#include "core_file.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <streams/file_stream.h>

struct xe_file {
  RFILE *stream;
  const uint8_t *mapping;
  uint64_t size;
  int64_t position;
};

xe_file *xe_file_open(const char *path) {
  xe_file *file;
  RFILE *stream;
  int64_t size, mapped_size = 0;
  if (!path || !*path) return NULL;
  stream = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
      RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
  if (!stream) return NULL;
  size = filestream_get_size(stream);
  if (size < 0) { filestream_close(stream); return NULL; }
  file = (xe_file *)calloc(1, sizeof(*file));
  if (!file) { filestream_close(stream); return NULL; }
  file->stream = stream;
  file->size = (uint64_t)size;
  file->position = -1;
  file->mapping = filestream_get_mapped_ptr(stream, &mapped_size);
  if (mapped_size != size || (uint64_t)size > (size_t)-1)
    file->mapping = NULL;
  return file;
}
void xe_file_close(xe_file *file) {
  if (!file) return;
  filestream_close(file->stream);
  free(file);
}
uint64_t xe_file_size(const xe_file *file) { return file ? file->size : 0; }
static int valid(const xe_file *file, uint64_t offset, size_t size) {
  return file && offset <= file->size && size <= file->size - offset;
}
const uint8_t *xe_file_view(const xe_file *file, uint64_t offset, size_t size) {
  if (!valid(file, offset, size) || !file->mapping) return NULL;
  return file->mapping + (size_t)offset;
}
int xe_file_read(xe_file *file, uint64_t offset, void *out, size_t size) {
  int64_t got;
  uint8_t *destination = (uint8_t *)out;
  if (!valid(file, offset, size) || (!out && size)) return 0;
  if (!size) return 1;
  if (file->mapping) {
    memcpy(out, file->mapping + (size_t)offset, size);
    return 1;
  }
  if (file->position != (int64_t)offset) {
    if (filestream_seek(file->stream, (int64_t)offset,
        RETRO_VFS_SEEK_POSITION_START) < 0) {
      file->position = -1; return 0;
    }
    file->position = (int64_t)offset;
  }
  while (size) {
    errno = 0;
    got = filestream_read(file->stream, destination, size);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0 || (uint64_t)got > size) { file->position = -1; return 0; }
    file->position += got;
    destination += (size_t)got;
    size -= (size_t)got;
  }
  return 1;
}
