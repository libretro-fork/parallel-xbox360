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
int xe_file_read_transfer(xe_file *file, uint64_t offset, void *out, size_t size, size_t *read) {
  int64_t got;
  uint8_t *destination = (uint8_t *)out;
  if (!read) return 0;
  *read = 0;
  if (!valid(file, offset, size) || (!out && size)) return 0;
  if (!size) return 1;
  if (file->mapping) {
    memcpy(out, file->mapping + (size_t)offset, size);
    *read = size;
    return 1;
  }
  if (file->position != (int64_t)offset) {
    if (filestream_seek(file->stream, (int64_t)offset,
        RETRO_VFS_SEEK_POSITION_START) != 0) {
      file->position = -1; return 0;
    }
    file->position = (int64_t)offset;
  }
  while (size) {
    errno = 0;
    got = filestream_read(file->stream, destination, size);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0 || (uint64_t)got > size) { file->position = -1; return 0; }
    *read += (size_t)got;
    file->position += got;
    destination += (size_t)got;
    size -= (size_t)got;
  }
  return 1;
}

int xe_file_read(xe_file *file, uint64_t offset, void *out, size_t size) {
  size_t read;
  return xe_file_read_transfer(file,offset,out,size,&read);
}

void xe_file_blob_close(xe_file_blob *blob) {
  if (!blob) return;
  free(blob->storage);
  xe_file_close(blob->owner);
  memset(blob, 0, sizeof(*blob));
}
int xe_file_blob_open(xe_file_blob *blob, const char *path) {
  uint64_t length;
  if (!blob || blob->owner || blob->storage || blob->data || blob->size) return 0;
  blob->owner = xe_file_open(path);
  if (!blob->owner) return 0;
  length = xe_file_size(blob->owner);
  if (length > SIZE_MAX) { xe_file_blob_close(blob); return 0; }
  blob->size = (size_t)length;
  if (!length) return 1;
  blob->data = xe_file_view(blob->owner, 0, blob->size);
  if (blob->data) return 1;
  blob->storage = malloc(blob->size);
  if (!blob->storage || !xe_file_read(blob->owner, 0, blob->storage, blob->size)) {
    xe_file_blob_close(blob); return 0;
  }
  blob->data = (const uint8_t *)blob->storage;
  return 1;
}

struct xe_file_records {
  xe_file *file;
  const uint8_t *mapping;
  uint8_t *window;
  uint64_t start;
  size_t record_size, count, window_records, window_first, window_count;
};
void xe_file_records_close(xe_file_records *records) {
  if (!records) return;
  xe_file_close(records->file);
  free(records->window);
  free(records);
}
xe_file_records *xe_file_records_open(const char *path, uint64_t start, size_t record_size) {
  xe_file_records *records;
  uint64_t size;
  if (!record_size) return NULL;
  records = (xe_file_records *)calloc(1, sizeof(*records));
  if (!records) return NULL;
  records->file = xe_file_open(path);
  if (!records->file) { xe_file_records_close(records); return NULL; }
  size = xe_file_size(records->file);
  if (start > size || size > SIZE_MAX) { xe_file_records_close(records); return NULL; }
  records->start = start; records->record_size = record_size;
  records->count = (size_t)((size - start) / record_size);
  if (!records->count) return records;
  records->mapping = xe_file_view(records->file, start, records->count * record_size);
  if (records->mapping) return records;
  records->window_records = 65536 / record_size;
  if (!records->window_records) records->window_records = 1;
  if (records->window_records > records->count) records->window_records = records->count;
  records->window = (uint8_t *)malloc(records->window_records * record_size);
  if (!records->window) { xe_file_records_close(records); return NULL; }
  return records;
}
size_t xe_file_records_count(const xe_file_records *records) { return records ? records->count : 0; }
size_t xe_file_records_stride(const xe_file_records *records) { return records ? records->record_size : 0; }
const uint8_t *xe_file_records_get(xe_file_records *records, size_t index) {
  size_t first, count;
  if (!records || index >= records->count) return NULL;
  if (records->mapping) return records->mapping + index * records->record_size;
  if (index < records->window_first || index - records->window_first >= records->window_count) {
    first = index - index % records->window_records;
    count = records->count - first;
    if (count > records->window_records) count = records->window_records;
    records->window_count = 0;
    if (!xe_file_read(records->file, records->start + first * records->record_size,
          records->window, count * records->record_size)) return NULL;
    records->window_first = first; records->window_count = count;
  }
  return records->window + (index - records->window_first) * records->record_size;
}

int xe_file_records_read(xe_file_records *records, size_t index, void *out, size_t size) {
  const uint8_t *bytes;
  if (!records || !out || size != records->record_size) return 0;
  bytes = xe_file_records_get(records, index);
  if (!bytes) return 0;
  memcpy(out, bytes, size);
  return 1;
}

struct xe_file_ranges {
  xe_file *file;
  uint8_t *storage;
  size_t capacity;
};
void xe_file_ranges_close(xe_file_ranges *ranges) {
  if (!ranges) return;
  xe_file_close(ranges->file);
  free(ranges->storage);
  free(ranges);
}
xe_file_ranges *xe_file_ranges_open(const char *path) {
  xe_file_ranges *ranges = (xe_file_ranges *)calloc(1, sizeof(*ranges));
  if (!ranges) return NULL;
  ranges->file = xe_file_open(path);
  if (!ranges->file) { xe_file_ranges_close(ranges); return NULL; }
  return ranges;
}
uint64_t xe_file_ranges_size(const xe_file_ranges *ranges) {
  return ranges ? xe_file_size(ranges->file) : 0;
}
const uint8_t *xe_file_ranges_get(xe_file_ranges *ranges, uint64_t offset, size_t size) {
  static const uint64_t empty = 0;
  const uint8_t *view;
  uint8_t *storage;
  if (!ranges || !valid(ranges->file, offset, size)) return NULL;
  if (!size) return (const uint8_t *)&empty;
  view = xe_file_view(ranges->file, offset, size);
  if (view) return view;
  if (size > ranges->capacity) {
    storage = (uint8_t *)malloc(size);
    if (!storage) return NULL;
    free(ranges->storage);
    ranges->storage = storage;
    ranges->capacity = size;
  }
  if (!xe_file_read(ranges->file, offset, ranges->storage, size)) return NULL;
  return ranges->storage;
}
