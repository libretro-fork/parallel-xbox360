#ifndef XENIA_LIBRETRO_CORE_LOG_LINE_H
#define XENIA_LIBRETRO_CORE_LOG_LINE_H

#include <stddef.h>
#include <string.h>

/* One logging writer owns this storage. Long lines are streamed in bounded
 * chunks; ignored severity levels never copy their payload. */
typedef struct xe_log_line {
  char text[1024];
  unsigned prefix_size;
  char prefix[3];
  size_t used;
  int severity;
} xe_log_line;

typedef void (*xe_log_line_emit)(void *, int, const char *, size_t, int);

static void xe_log_line_write(xe_log_line *line, const char *data, size_t size,
                              xe_log_line_emit emit, void *context) {
  size_t count;
  const char *end;
  while (size) {
    if (*data == '\n') {
      if (line->severity)
        emit(context, line->severity, line->text, line->used, 1);
      line->prefix_size = 0;
      line->severity = 0;
      line->used = 0;
      ++data;
      --size;
      continue;
    }
    if (line->prefix_size < sizeof(line->prefix)) {
      line->prefix[line->prefix_size++] = *data++;
      --size;
      if (line->prefix_size == sizeof(line->prefix) && line->prefix[1] == '>')
        line->severity = line->prefix[0] == '!' ? 2 :
                         line->prefix[0] == 'w' ? 1 : 0;
      continue;
    }
    end = (const char *)memchr(data, '\n', size);
    count = end ? (size_t)(end - data) : size;
    if (line->severity) {
      if (count > sizeof(line->text) - line->used)
        count = sizeof(line->text) - line->used;
      memcpy(line->text + line->used, data, count);
      line->used += count;
      if (line->used == sizeof(line->text)) {
        emit(context, line->severity, line->text, line->used, 0);
        line->used = 0;
      }
    }
    data += count;
    size -= count;
  }
}

#endif
