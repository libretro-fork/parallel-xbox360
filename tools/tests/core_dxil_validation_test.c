/* CPU-only optional DXIL validator runner. List contains one path per line.
 * SDK COM calls are isolated in the ABI bridge; all test algorithms are C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void *xe_test_validator_open(const char *);
extern int xe_test_validator_check(void *, void *, size_t, void (*)(const char *, size_t));
extern void xe_test_validator_close(void *);
static void report(const char *data, size_t size) { fwrite(data, 1, size, stderr); }
int main(int argc, char **argv) {
  FILE *list, *file;
  char path[4096];
  void *validator, *data;
  unsigned count = 0;
  int okay = 1;
  if (argc != 3) return 2;
  validator = xe_test_validator_open(argv[1]);
  if (!validator) { fprintf(stderr, "Cannot load validator: %s\n", argv[1]); return 2; }
  list = fopen(argv[2], "rb");
  if (!list) { xe_test_validator_close(validator); return 2; }
  while (fgets(path, sizeof(path), list)) {
    size_t length = strlen(path);
    long bytes;
    if (!length || (length == sizeof(path) - 1 && path[length - 1] != '\n')) { okay = 0; break; }
    while (length && (path[length - 1] == '\n' || path[length - 1] == '\r')) path[--length] = 0;
    if (!length) continue;
    file = fopen(path, "rb");
    if (!file) { okay = 0; break; }
    if (fseek(file, 0, SEEK_END) || (bytes = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET)) {
      fclose(file); okay = 0; break;
    }
    data = malloc((size_t)bytes);
    if (!data) { fclose(file); okay = 0; break; }
    okay = fread(data, 1, (size_t)bytes, file) == (size_t)bytes;
    if (fclose(file)) okay = 0;
    if (okay) okay = xe_test_validator_check(validator, data, (size_t)bytes, report);
    free(data);
    if (!okay) break;
    ++count;
  }
  if (ferror(list)) okay = 0;
  if (fclose(list)) okay = 0;
  xe_test_validator_close(validator);
  if (!okay) { fprintf(stderr, "Validation failed at: %s\n", path); return 1; }
  printf("PASS: Microsoft DXIL validation of %u shaders, no GPU/device created\n", count);
  return count ? 0 : 1;
}
