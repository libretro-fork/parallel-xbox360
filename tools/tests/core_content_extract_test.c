/* Exercise the real RFILE adapter with hostile frontend transfers. C89. */
#include <stdio.h>
#include <string.h>
#include "core_content_extract.h"
#include <file/file_path.h>
#include <rthreads/rthreads.h>
#include <streams/file_stream.h>
enum { PAYLOAD = 196711 };
static unsigned char input[PAYLOAD + 64], staged[PAYLOAD + 64], final[PAYLOAD + 64];
static size_t position, final_size, read_limit;
static int exists, fault, mapped, borrowed, bad, opens, closes, removes, renames;
static unsigned reads, views;
static void *scratch;
static char temp_name[16384], previous_name[16384];
static const char *destination = "cache://root/game.xex";
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int32_t RETRO_CALLCONV stat_file(const char *path, int32_t *size) {
  if (size) *size = 0;
  if (!strcmp(path, "cache://root"))
    return RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY;
  if (!strcmp(path, destination) && exists) {
    if (size) *size = (int32_t)final_size;
    return RETRO_VFS_STAT_IS_VALID;
  }
  return 0;
}
static int RETRO_CALLCONV mkdir_file(const char *path) { (void)path; return -1; }
static struct retro_vfs_file_handle *RETRO_CALLCONV open_file(
    const char *path, unsigned mode, unsigned hints) {
  ++opens;
  if (fault == 8) return NULL;
  if (mode != RETRO_VFS_FILE_ACCESS_WRITE ||
      hints != RETRO_VFS_FILE_ACCESS_HINT_SEQUENTIAL_BULK ||
      !strstr(path, "game.xex.px360-") || strlen(path) >= sizeof(temp_name)) {
    bad = 1; return NULL;
  }
  strcpy(temp_name, path); position = 0;
  return (struct retro_vfs_file_handle *)staged;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle *file) {
  (void)file; ++closes; return fault == 6 ? -1 : 0;
}
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle *file) {
  (void)file; return (int64_t)position;
}
static int64_t RETRO_CALLCONV write_file(struct retro_vfs_file_handle *file,
    const void *data, uint64_t size) {
  size_t n = size > 997 ? 997 : (size_t)size;
  (void)file;
  if (fault == 4) return 0;
  if (fault == 5) return -1;
  if (fault == 10) return (int64_t)size + 1;
  if (borrowed && data != input + position) bad = 1;
  if (n > sizeof(staged) - position) { bad = 1; return -1; }
  memcpy(staged + position, data, n); position += n;
  return (int64_t)n;
}
static int RETRO_CALLCONV rename_file(const char *from, const char *to) {
  ++renames;
  if (closes != 1 || strcmp(from, temp_name) ||
      strcmp(to, destination)) bad = 1;
  if (fault == 7) return -1;
  memcpy(final, staged, position); final_size = position; exists = 1;
  return 0;
}
static int RETRO_CALLCONV remove_file(const char *path) {
  ++removes;
  if (!strstr(path, ".px360-") || !strcmp(path, "cache://root/game.xex")) bad = 1;
  return 0;
}
static const uint8_t *view_source(void *opaque, uint64_t *available) {
  (void)opaque; ++views;
  *available = mapped == 2 ? 3 : sizeof(input);
  return mapped ? input : NULL;
}
static int read_source(void *opaque, uint64_t offset, void *data,
    size_t wanted, size_t *received) {
  size_t n = wanted;
  (void)opaque; ++reads;
  if (wanted > 65536 || offset > PAYLOAD || wanted > PAYLOAD - offset) bad = 1;
  if (scratch && scratch != data) bad = 1;
  scratch = data;
  if (fault == 1) return 0;
  if (fault == 2 || (fault == 9 && offset)) { *received = 0; return 1; }
  if (fault == 3) { *received = wanted + 1; return 1; }
  if (n > read_limit) n = read_limit;
  memcpy(data, input + (size_t)offset, n); *received = n;
  return 1;
}
static void reset(int failure, int map) {
  fault = failure; mapped = map; borrowed = map == 1;
  position = 0; final_size = 7; exists = 1;
  memcpy(final, "oldfile", 7);
  reads = views = 0; scratch = NULL; read_limit = 65536;
  bad = opens = closes = removes = renames = 0;
}
struct native_job { const char *directory; unsigned index; int ok; };
static const uint8_t *native_view(void *opaque, uint64_t *available) {
  (void)opaque; *available = sizeof(input); return input;
}
static void native_worker(void *opaque) {
  struct native_job *job = (struct native_job *)opaque;
  xe_content_source source;
  unsigned i;
  source.opaque = NULL; source.view = native_view; source.read = NULL;
  job->ok = 0;
  for (i = 0; i < 16; ++i) {
    source.size = PAYLOAD - ((job->index + i) & 3);
    if (xe_content_extract_file(&source, job->directory, "game.xex", 8))
      ++job->ok;
  }
}
int main(int argc, char **argv) {
  struct retro_vfs_interface iface;
  struct retro_vfs_interface_info info;
  xe_content_source source;
  RFILE *file;
  unsigned char block[4096];
  static char long_root[5000], long_destination[5100];
  struct native_job jobs[4];
  sthread_t *threads[4];
  int64_t native_size;
  size_t i;
  int failure, map, completed;
  memset(&iface, 0, sizeof(iface));
  iface.open = open_file; iface.close = close_file; iface.size = size_file;
  iface.write = write_file; iface.rename = rename_file; iface.remove = remove_file;
  iface.stat = stat_file; iface.mkdir = mkdir_file;
  info.required_interface_version = 3; info.iface = &iface;
  filestream_vfs_init(&info);
  path_vfs_init(&info);
  for (i = 0; i < sizeof(input); ++i) input[i] = (unsigned char)(i * 37 + i / 127);
  source.opaque = NULL; source.size = PAYLOAD;
  source.view = view_source; source.read = read_source;
  for (map = 0; map < 3; ++map) {
    reset(0, map);
    CHECK(xe_content_extract_file(&source, "cache://root", "game.xex", 8));
    CHECK(!bad && final_size == PAYLOAD && !memcmp(input, final, PAYLOAD));
    CHECK(opens == 1 && closes == 1 && renames == 1 && !removes);
    CHECK(map == 1 ? reads == 0 : reads == 4);
    if (map) CHECK(strcmp(previous_name, temp_name));
    strcpy(previous_name, temp_name);
    opens = 0; reads = views = 0;
    CHECK(xe_content_extract_file(&source, "cache://root", "game.xex", 8));
    CHECK(!opens && !reads && !views);
  }
  reset(0, 0); read_limit = 313;
  CHECK(xe_content_extract_file(&source, "cache://root", "game.xex", 8));
  CHECK(!bad && !memcmp(input, final, PAYLOAD) && reads > 4);
  for (failure = 1; failure <= 10; ++failure) {
    reset(failure, 0);
    CHECK(!xe_content_extract_file(&source, "cache://root", "game.xex", 8));
    CHECK(!bad && final_size == 7 && !memcmp(final, "oldfile", 7));
    CHECK(removes == 1 && (failure == 8 ? !closes : closes == 1));
  }
  for (failure = 4; failure <= 10; ++failure) {
    if (failure == 9) continue;
    reset(failure, 1);
    CHECK(!xe_content_extract_file(&source, "cache://root", "game.xex", 8));
    CHECK(!bad && !reads && final_size == 7 && !memcmp(final, "oldfile", 7));
    CHECK(removes == 1);
  }
  reset(0, 0); source.read = NULL;
  CHECK(!xe_content_extract_file(&source, "cache://root", "game.xex", 8));
  CHECK(!bad && !reads && final_size == 7 && removes == 1);
  source.read = read_source;
  reset(0, 0); source.view = NULL;
  CHECK(xe_content_extract_file(&source, "cache://root", "game.xex", 8));
  CHECK(!bad && !views && !memcmp(final, input, PAYLOAD));
  source.view = view_source;
  reset(0, 1); final_size = PAYLOAD + 1;
  CHECK(xe_content_extract_file(&source, "cache://root", "game.xex", 8));
  CHECK(!bad && final_size == PAYLOAD && !memcmp(final, input, PAYLOAD));
  reset(0, 0); source.size = 0;
  CHECK(xe_content_extract_file(&source, "cache://root", "game.xex", 8));
  CHECK(!bad && !final_size && !reads && !views);
  reset(0, 0); source.size = UINT64_MAX;
  CHECK(!xe_content_extract_file(&source, "cache://root", "game.xex", 8));
  source.size = PAYLOAD;
  CHECK(!xe_content_extract_file(&source, "cache://root", "x", (size_t)-1));
  CHECK(!xe_content_extract_file(&source, "cache://root", "../x", 4));
  CHECK(!xe_content_extract_file(&source, "cache://root", "a:b", 3));
  CHECK(!xe_content_leaf_valid("a\0b", 3));
  CHECK(!xe_content_leaf_valid("..", 2));
  CHECK(!xe_content_leaf_valid("a\\b", 3));
  CHECK(!opens);
  CHECK(!xe_content_extract_file(NULL, "cache://root", "x", 1));
  CHECK(!xe_content_extract_file(&source, "", "x", 1));
  memset(long_root, 'x', sizeof(long_root) - 1);
  strcpy(long_destination, long_root); strcat(long_destination, "/game.xex");
  destination = long_destination;
  reset(0, 1);
  CHECK(xe_content_extract_file(&source, long_root, "game.xex", 8));
  CHECK(!bad && !memcmp(input, final, PAYLOAD));
  /* Native VFS replacement: repair a short file, then verify exact bytes. */
  if (argc > 1) {
    info.iface = NULL; filestream_vfs_init(&info); path_vfs_init(&info);
    mapped = 1; source.size = 7;
    CHECK(xe_content_extract_file(&source, argv[1], "game.xex", 8));
    source.size = PAYLOAD;
    CHECK(xe_content_extract_file(&source, argv[1], "game.xex", 8));
    CHECK(strlen(argv[1]) + 10 < sizeof(temp_name));
    strcpy(temp_name, argv[1]); strcat(temp_name, "/game.xex");
    for (i = 0; i < 4; ++i) {
      jobs[i].directory = argv[1]; jobs[i].index = (unsigned)i; jobs[i].ok = 0;
      threads[i] = sthread_create(native_worker, &jobs[i]);
      CHECK(threads[i]);
    }
    completed = 0;
    for (i = 0; i < 4; ++i) { sthread_join(threads[i]); completed += jobs[i].ok; }
    /* Windows can refuse competing renames. Failure must preserve a complete
     * published image; it must never remove that image to force a retry. */
    CHECK(completed);
    file = filestream_open(temp_name, RETRO_VFS_FILE_ACCESS_READ, 0);
    CHECK(file);
    native_size = filestream_get_size(file);
    CHECK(native_size >= PAYLOAD - 3 && native_size <= PAYLOAD);
    for (i = 0; i < (size_t)native_size; ) {
      size_t n = (size_t)native_size - i < sizeof(block) ?
          (size_t)native_size - i : sizeof(block);
      CHECK(filestream_read(file, block, (int64_t)n) == (int64_t)n);
      CHECK(!memcmp(input + i, block, n)); i += n;
    }
    CHECK(!filestream_close(file)); CHECK(!filestream_delete(temp_name));
    CHECK(path_rmdir(argv[1]));
  }
  puts("Content extraction: borrowed maps, bounded scratch, partial transfers, cache reuse and fault cleanup pass");
  return 0;
}
