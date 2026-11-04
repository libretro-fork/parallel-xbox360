#include "libretro/core_log_line.h"
#include <assert.h>
#include <stdio.h>

typedef struct capture {
  char text[8192];
  size_t used;
  unsigned errors, warnings, lines;
} capture;

static void emit(void *context, int severity, const char *text, size_t size,
                 int newline) {
  capture *result = (capture *)context;
  assert(result->used + size < sizeof(result->text));
  memcpy(result->text + result->used, text, size);
  result->used += size;
  if (severity == 2) ++result->errors;
  else ++result->warnings;
  if (newline) result->text[result->used++] = '\n', ++result->lines;
}

int main(void) {
  xe_log_line line = {0};
  capture result = {{0}, 0, 0, 0, 0};
  char input[5005];
  size_t split, i;
  const char sample[] = "i> discard\n!> error\nw> warning\n!\n\n";
  for (split = 0; split < sizeof(sample); ++split) {
    memset(&line, 0, sizeof(line));
    memset(&result, 0, sizeof(result));
    xe_log_line_write(&line, sample, split, emit, &result);
    xe_log_line_write(&line, sample + split, sizeof(sample) - 1 - split,
                      emit, &result);
    assert(result.used == 14 && !memcmp(result.text, "error\nwarning\n", 14));
    assert(result.errors == 1 && result.warnings == 1 && result.lines == 2);
  }
  memset(&line, 0, sizeof(line));
  memset(&result, 0, sizeof(result));
  memcpy(input, "w> ", 3);
  memset(input + 3, 'x', 5000);
  input[5003] = '\n';
  for (i = 0; i < 5004; ++i)
    xe_log_line_write(&line, input + i, 1, emit, &result);
  assert(result.used == 5001 && result.lines == 1);
  for (i = 0; i < 5000; ++i) assert(result.text[i] == 'x');
  assert(result.text[5000] == '\n');
  puts("PASS: fragmented prefixes, ignored levels and long lines use bounded reusable storage");
  return 0;
}
