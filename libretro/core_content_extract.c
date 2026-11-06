#include "core_content_extract.h"
#include <stdlib.h>
#include <string.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include <retro_atomic.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifndef RETRO_ATOMIC_HAS_64
#error Content extraction requires libretro-common 64-bit atomics
#endif
#if defined(_MSC_VER)
#define XE_EXTRACT_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XE_EXTRACT_NOINLINE __attribute__((noinline))
#else
#define XE_EXTRACT_NOINLINE
#endif
#ifndef XE_EXTRACT_ALLOC
#define XE_EXTRACT_ALLOC malloc
#define XE_EXTRACT_FREE free
#endif
enum { XE_EXTRACT_NAMES = 8192, XE_EXTRACT_SCRATCH = 65536 };
static retro_atomic_64_t extraction_ticket;

int xe_content_leaf_valid(const char *name, size_t length) {
  size_t i;
  if (!name || !length || (length == 1 && name[0] == '.') ||
      (length == 2 && name[0] == '.' && name[1] == '.')) return 0;
  for (i = 0; i < length; ++i)
    if (!name[i] || name[i] == '/' || name[i] == '\\' || name[i] == ':')
      return 0;
  return 1;
}
static uint64_t next_ticket(void) {
  int64_t previous;
  do {
    previous = retro_atomic_load_relaxed_64(&extraction_ticket);
    /* Saturate rather than ever reusing a live staging name. */
    if (previous < 0 || previous == INT64_MAX) return 0;
  } while (!retro_atomic_cas_64(&extraction_ticket, previous, previous + 1));
  return (uint64_t)previous + 1;
}
static void hex_digits(char *out, uint64_t value, unsigned count) {
  static const char digits[] = "0123456789abcdef";
  unsigned i;
  for (i = 0; i < count; ++i)
    out[i] = digits[(value >> ((count - 1 - i) * 4)) & 15];
}
static int write_all(RFILE *file, const uint8_t *data, size_t length) {
  int64_t written;
  while (length) {
    written = filestream_write(file, data, (int64_t)length);
    if (written <= 0 || (uint64_t)written > length) return 0;
    data += (size_t)written;
    length -= (size_t)written;
  }
  return 1;
}
/* Separate frame: mapped transfers do not probe/touch payload scratch pages. */
static XE_EXTRACT_NOINLINE int copy_buffered(const xe_content_source *source,
    RFILE *output) {
  uint8_t scratch[XE_EXTRACT_SCRATCH];
  uint64_t offset = 0, remaining;
  size_t wanted, received;
  if (!source->read) return 0;
  while (offset < source->size) {
    remaining = source->size - offset;
    wanted = remaining < sizeof(scratch) ? (size_t)remaining : sizeof(scratch);
    received = 0;
    if (!source->read(source->opaque, offset, scratch, wanted, &received) ||
        !received || received > wanted || !write_all(output, scratch, received))
      return 0;
    offset += received;
  }
  return 1;
}
int xe_content_extract_file(const xe_content_source *source,
    const char *directory, const char *name, size_t name_length) {
  static const char prefix[] = ".px360-";
  char local[XE_EXTRACT_NAMES];
  char *names = local, *temporary = NULL, *suffix;
  size_t root_length, length, total, separator, bytes;
  uint64_t ticket, process, available = 0, offset;
  const uint8_t *view = NULL;
  RFILE *output = NULL;
  int ok = 0, owns_temporary = 0;
  /* Two names: directory/leaf and directory/leaf.px360-PID-TICKET.tmp. */
  const size_t tail = sizeof(prefix) - 1 + 8 + 1 + 16 + sizeof(".tmp");
  if (!source || !directory || !*directory || !name ||
      source->size > INT64_MAX || source->size > (size_t)-1) return 0;
  root_length = strlen(directory);
  if (root_length > ((size_t)-1 - tail - 4) / 2 ||
      name_length > ((size_t)-1 - tail - 4) / 2 - root_length) return 0;
  if (!xe_content_leaf_valid(name, name_length)) return 0;
  separator = directory[root_length - 1] != '/' && directory[root_length - 1] != '\\';
  length = root_length + separator + name_length;
  total = 2 * length + tail + 1;
  if (total > sizeof(local)) {
    names = (char*)XE_EXTRACT_ALLOC(total);
    if (!names) return 0;
  }
  memcpy(names, directory, root_length);
  if (separator) names[root_length] = '/';
  memcpy(names + root_length + separator, name, name_length);
  names[length] = 0;
  if (path_get_size(names) == (int64_t)source->size && !path_is_directory(names)) {
    ok = 1;
    goto done;
  }
  ticket = next_ticket();
  if (!ticket) goto done;
#ifdef _WIN32
  process = GetCurrentProcessId();
#else
  process = (uint64_t)getpid();
#endif
  temporary = names + length + 1;
  memcpy(temporary, names, length);
  suffix = temporary + length;
  memcpy(suffix, prefix, sizeof(prefix) - 1);
  suffix += sizeof(prefix) - 1;
  hex_digits(suffix, process, 8);
  suffix[8] = '-';
  hex_digits(suffix + 9, ticket, 16);
  memcpy(suffix + 25, ".tmp", sizeof(".tmp"));
  owns_temporary = 1;
  output = filestream_open(temporary, RETRO_VFS_FILE_ACCESS_WRITE,
      RETRO_VFS_FILE_ACCESS_HINT_SEQUENTIAL_BULK);
  /* Existing opaque frontend directories need no native stat/mkdir support. */
  if (!output && path_mkdir(directory))
    output = filestream_open(temporary, RETRO_VFS_FILE_ACCESS_WRITE,
        RETRO_VFS_FILE_ACCESS_HINT_SEQUENTIAL_BULK);
  if (!output) goto done;
  if (!source->size) ok = 1;
  else {
    if (source->view) view = source->view(source->opaque, &available);
    if (view && available >= source->size) {
      ok = 1;
      for (offset = 0; offset < source->size; offset += bytes) {
        bytes = source->size - offset < 1048576 ?
            (size_t)(source->size - offset) : 1048576;
        if (!write_all(output, view + (size_t)offset, bytes)) { ok = 0; break; }
      }
    } else ok = copy_buffered(source, output);
  }
done:
  if (output && filestream_close(output) != 0) ok = 0;
  if (owns_temporary) {
    if (ok && filestream_rename(temporary, names) == 0) owns_temporary = 0;
    else ok = 0;
    if (owns_temporary) filestream_delete(temporary);
  }
  if (names != local) XE_EXTRACT_FREE(names);
  return ok;
}
