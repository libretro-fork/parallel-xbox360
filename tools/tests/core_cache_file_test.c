/* Cache ownership/repair and hostile VFS transfers; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "core_cache_file.h"
#include "core_cache_writer.h"
#include "core_file.h"
#include <encodings/xxh3.h>
#ifdef XE_TEST_SHADER_STORAGE
#include <encodings/xxh3.h>
extern uint64_t xe_test_shader_entries(RFILE *file, unsigned *entries);
extern uint64_t xe_test_shader_entries_path(const char *path, unsigned *entries);
#endif
static unsigned char storage[4096];
static size_t length, position;
static int exists, fault, interrupted;
static unsigned truncates;
static struct retro_vfs_file_handle *RETRO_CALLCONV open_file(const char *path, unsigned mode, unsigned hints) {
   (void)hints;
   if (strcmp(path, "cache://pipeline")) return NULL;
   if (mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) {
      if (!exists || fault == 5) return NULL;
   } else if (mode == RETRO_VFS_FILE_ACCESS_READ) {
      if (!exists) return NULL;
   } else { exists = 1; length = 0; }
   position = 0; return (struct retro_vfs_file_handle *)storage;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle *file) {(void)file; return 0;}
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle *file) {(void)file; return (int64_t)length;}
static int64_t RETRO_CALLCONV tell_file(struct retro_vfs_file_handle *file) {(void)file; return (int64_t)position;}
static int64_t RETRO_CALLCONV seek_file(struct retro_vfs_file_handle *file, int64_t offset, int origin) {
   int64_t next;
   (void)file;
   if (fault == 3) return -1;
   next = offset + (origin == RETRO_VFS_SEEK_POSITION_CURRENT ? (int64_t)position :
         origin == RETRO_VFS_SEEK_POSITION_END ? (int64_t)length : 0);
   if (next < 0 || next > (int64_t)sizeof(storage)) return -1;
   position = (size_t)next; return next;
}
static int64_t RETRO_CALLCONV read_file(struct retro_vfs_file_handle *file, void *out, uint64_t size) {
   size_t n = size > 7 ? 7 : (size_t)size;
   (void)file;
   if (fault == 7) { errno = EIO; return -1; }
   if (position >= length) return 0;
   if (interrupted) { interrupted = 0; errno = EINTR; return -1; }
   if (n > length - position) n = length - position;
   memcpy(out, storage + position, n); position += n; return (int64_t)n;
}
static int64_t RETRO_CALLCONV write_file(struct retro_vfs_file_handle *file, const void *data, uint64_t size) {
   size_t n = size > 5 ? 5 : (size_t)size;
   (void)file;
   if (fault == 1) { errno = ENOSPC; return -1; }
   if (fault == 6) return 0;
   if (interrupted) { interrupted = 0; errno = EINTR; return -1; }
   if (n > sizeof(storage) - position) return -1;
   memcpy(storage + position, data, n); position += n;
   if (position > length) length = position;
   return (int64_t)n;
}
static int RETRO_CALLCONV flush_file(struct retro_vfs_file_handle *file) {(void)file; return fault == 4 ? -1 : 0;}
static int64_t RETRO_CALLCONV truncate_file(struct retro_vfs_file_handle *file, int64_t size) {
   (void)file; ++truncates;
   if (fault == 2 || size < 0 || size > (int64_t)sizeof(storage)) return -1;
   length = (size_t)size; return 0;
}
/* Exercise the actual async queue -> transfer helper -> VFS path. */
struct writer_file_state { RFILE *file; unsigned rows, flushes; int ok; };
static void writer_file_sink(void *opaque, unsigned kind, const void *data, size_t size) {
   struct writer_file_state *state = (struct writer_file_state *)opaque;
   if (kind == 2) {
      if (size || filestream_flush(state->file)) state->ok = 0;
      ++state->flushes;
   } else {
      if (kind != 1 || size != 48 ||
            xe_cache_file_write(data, size, 1, state->file) != 1) state->ok = 0;
      ++state->rows;
   }
}
static int writer_file_test(const char *path) {
   struct writer_file_state state;
   xe_cache_writer *writer;
   uint8_t payload[40], actual[48], expected[48];
   uint64_t hash;
   unsigned r, i;
   state.file = xe_cache_file_open(path);
   state.rows = state.flushes = 0; state.ok = 1;
   if (!state.file || !xe_cache_file_repair(state.file, 0)) return 0;
   writer = xe_cache_writer_new(48, 4, writer_file_sink, &state);
   if (!writer) return 0;
   if (!strcmp(path, "cache://pipeline")) interrupted = 1;
   for (r = 0; r < 64; ++r) {
      for (i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(r * 13 + i * 37);
      hash = xxh3_64(payload, sizeof(payload), 0);
      if (!xe_cache_writer_submit_parts(writer, 1, &hash, sizeof(hash),
            payload, sizeof(payload))) return 0;
      memset(payload, 0xcc, sizeof(payload));
      if ((r + 1) % 8 == 0 && !xe_cache_writer_submit(writer, 2, NULL, 0)) return 0;
   }
   xe_cache_writer_free(writer);
   if (!state.ok || state.rows != 64 || state.flushes != 8 ||
         filestream_seek(state.file, 0, RETRO_VFS_SEEK_POSITION_START) < 0) return 0;
   for (r = 0; r < 64; ++r) {
      for (i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(r * 13 + i * 37);
      hash = xxh3_64(payload, sizeof(payload), 0);
      memcpy(expected, &hash, sizeof(hash));
      memcpy(expected + sizeof(hash), payload, sizeof(payload));
      if (xe_cache_file_read(actual, sizeof(actual), 1, state.file) != 1 ||
            memcmp(actual, expected, sizeof(actual))) return 0;
   }
   filestream_close(state.file);
   return 1;
}
static unsigned sink_calls;
static int sink_reject;
static int accept_row(void *opaque, const uint8_t *row, size_t size) {
   (void)opaque; (void)row;
   ++sink_calls;
   return size == 48 && !sink_reject;
}
static int validation_test(const char *path) {
   uint8_t rows[3][48], prefix[3] = {'P', 'S', 'O'};
   uint64_t hash;
   unsigned r, i;
   size_t valid;
   RFILE *file;
   xe_file_records *records;
   for (r = 0; r < 3; ++r) {
      for (i = 8; i < 48; ++i) rows[r][i] = (uint8_t)(r * 13 + i * 37);
      hash = xxh3_64(rows[r] + 8, 40, 0); memcpy(rows[r], &hash, sizeof(hash));
   }
   file = xe_cache_file_open(path);
   if (!file || !xe_cache_file_repair(file, 0) || xe_cache_file_write(prefix, 1, 3, file) != 3 ||
       xe_cache_file_write(rows, 48, 3, file) != 3 || filestream_flush(file)) return 0;
   filestream_close(file);
   records = xe_file_records_open(path, 3, 48); if (!records) return 0;
   sink_calls = 0; sink_reject = 0;
   if (!xe_cache_records_validate(records, 48, 0, 8, 40, accept_row, NULL, &valid) || valid != 3 || sink_calls != 3) return 0;
   if (xe_cache_records_validate(records, 49, 0, 8, 40, accept_row, NULL, &valid) || valid ||
       xe_cache_records_validate(records, 48, SIZE_MAX, 8, 40, accept_row, NULL, &valid) ||
       xe_cache_records_validate(records, 48, 0, 8, SIZE_MAX, accept_row, NULL, &valid) || sink_calls != 3) return 0;
   sink_reject = 1; sink_calls = 0;
   if (xe_cache_records_validate(records, 48, 0, 8, 40, accept_row, NULL, &valid) || valid || sink_calls != 1) return 0;
   sink_reject = 0; xe_file_records_close(records);
   rows[1][0] ^= 0x80;
   file = xe_cache_file_open(path);
   if (!file || !xe_cache_file_repair(file, 3) || xe_cache_file_write(rows, 48, 3, file) != 3 || filestream_flush(file)) return 0;
   filestream_close(file);
   records = xe_file_records_open(path, 3, 48); if (!records) return 0;
   sink_calls = 0;
   if (!xe_cache_records_validate(records, 48, 0, 8, 40, accept_row, NULL, &valid) || valid != 1 || sink_calls != 1) return 0;
   xe_file_records_close(records);
   if (!strcmp(path, "cache://pipeline")) {
      records = xe_file_records_open(path, 3, 48); if (!records) return 0;
      fault = 7;
      if (xe_cache_records_validate(records, 48, 0, 8, 40, accept_row, NULL, &valid) || valid) return 0;
      fault = 0; xe_file_records_close(records);
   }
   return 1;
}

int main(int argc, char **argv) {
   struct retro_vfs_interface iface;
   struct retro_vfs_interface_info info;
   unsigned char data[100], copy[102];
   unsigned i, before;
   uint32_t words[1057];
   unsigned char encoded[1057 * 4];
   RFILE *file;
   xe_file_records *snapshot;
#ifdef XE_TEST_SHADER_STORAGE
   unsigned entries;
   uint64_t hash;
   uint32_t words_count;
   unsigned char record[20];
#endif
   if (argc != 2) return 1;
   for (i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)(i * 37);
   memset(&iface, 0, sizeof(iface));
   iface.open = open_file; iface.close = close_file; iface.size = size_file;
   iface.tell = tell_file; iface.seek = seek_file; iface.read = read_file;
   iface.write = write_file; iface.flush = flush_file; iface.truncate = truncate_file;
   info.required_interface_version = FILESTREAM_REQUIRED_VFS_VERSION; info.iface = &iface;
   filestream_vfs_init(&info);
   file = xe_cache_file_open("cache://pipeline"); if (!file) return 2;
   interrupted = 1;
   if (xe_cache_file_write(data, 4, 25, file) != 25 || length != 100) return 3;
   if (filestream_seek(file, 0, RETRO_VFS_SEEK_POSITION_START) != 0) return 4;
   memset(copy, 0xAD, sizeof(copy)); interrupted = 1;
   if (!xe_cache_file_has_bytes(file, 100) || xe_cache_file_has_bytes(file, 101) ||
         xe_cache_file_has_bytes(file, UINT64_MAX)) return 5;
   if (xe_cache_file_read(copy + 1, 4, 25, file) != 25 || memcmp(copy + 1, data, 100) ||
         copy[0] != 0xAD || copy[101] != 0xAD) return 6;
   if (!xe_cache_file_repair(file, 50) || filestream_tell(file) != 50 ||
         xe_cache_file_write(data, 1, 10, file) != 10 || length != 60) return 7;
   filestream_close(file);
   fault = 5;
   if (xe_cache_file_open("cache://pipeline") || length != 60) return 8;
   fault = 0; file = xe_cache_file_open("cache://pipeline"); if (!file || length != 60) return 9;
   if (xe_cache_file_read(copy, 1, 60, file) != 60 || memcmp(copy, data, 50) || memcmp(copy + 50, data, 10)) return 10;
   if (xe_cache_file_write(data, SIZE_MAX, 2, file) || xe_cache_file_read(copy, 2, SIZE_MAX, file) ||
         xe_cache_file_repair(file, UINT64_MAX)) return 11;
   for (fault = 1; fault <= 6; ++fault) {
      if (fault == 1 || fault == 6) { if (xe_cache_file_write(data, 1, 10, file)) return 12; }
      else if (fault != 5) {
         before = truncates;
         if (xe_cache_file_repair(file, 40)) return 13;
         if (fault == 4 && before != truncates) return 14;
      }
   }
   fault = 0; filestream_close(file);
   if (!validation_test("cache://pipeline")) return 33;
   if (!writer_file_test("cache://pipeline")) return 35;
#ifdef XE_TEST_SHADER_STORAGE
   memset(storage, 0, sizeof(storage));
   length = 40; exists = 1; fault = 0;
   words_count = 2;
   memcpy(storage + 16, &words_count, 4);
   memcpy(storage + 20, data, 8);
   hash = xxh3_64(storage + 20, 8, 0);
   memcpy(storage + 8, &hash, 8);
   words_count = 0;
   memcpy(storage + 36, &words_count, 4);
   hash = xxh3_64(storage + 40, 0, 0);
   memcpy(storage + 28, &hash, 8);
   interrupted = 1;
   if (xe_test_shader_entries_path("cache://pipeline", &entries) != 40 || entries != 2) return 37;
   before = truncates; fault = 7;
   if (xe_test_shader_entries_path("cache://pipeline", &entries) != UINT64_MAX ||
         entries || truncates != before || length != 40) return 38;
   fault = 0; storage[8] ^= 1;
   if (xe_test_shader_entries_path("cache://pipeline", &entries) != 8 || entries) return 39;
   storage[8] ^= 1; length = 27;
   if (xe_test_shader_entries_path("cache://pipeline", &entries) != 8 || entries) return 40;
#endif
   info.iface = NULL; filestream_vfs_init(&info);
   file = xe_cache_file_open(argv[1]); if (!file || !xe_cache_file_repair(file, 0)) return 15;
   if (xe_cache_file_write(data, 1, 100, file) != 100) return 16;
   filestream_close(file); file = xe_cache_file_open(argv[1]);
   if (!file) return 17;
   if (xe_cache_file_read(copy, 1, 100, file) != 100 || memcmp(copy, data, 100)) return 18;
   if (!xe_cache_file_repair(file, 50)) { fprintf(stderr, "native repair failed errno=%d\n", errno); return 19; }
   if (xe_cache_file_write(data, 1, 10, file) != 10) return 20;
   filestream_close(file);
   file = xe_cache_file_open(argv[1]);
   if (!file || !xe_cache_file_repair(file, 0)) return 21;
   for (i = 0; i < 1057; ++i) words[i] = UINT32_C(0x10203040) + i * 37;
   if (!xe_cache_file_write_be32(file, words, 1057)) return 22;
   filestream_close(file); file = xe_cache_file_open(argv[1]);
   if (!file || xe_cache_file_read(encoded, 1, sizeof(encoded), file) != sizeof(encoded)) return 23;
   for (i = 0; i < 1057; ++i) {
      if (encoded[i * 4] != (unsigned char)(words[i] >> 24) ||
          encoded[i * 4 + 1] != (unsigned char)(words[i] >> 16) ||
          encoded[i * 4 + 2] != (unsigned char)(words[i] >> 8) ||
          encoded[i * 4 + 3] != (unsigned char)words[i]) return 24;
   }
   if (filestream_flush(file)) return 29;
   snapshot = xe_file_records_open(argv[1], 0, 4);
   if (!snapshot || xe_file_records_count(snapshot) != 1057 ||
       !xe_file_records_get(snapshot, 1056)) return 30;
   if (filestream_seek(file, 0, RETRO_VFS_SEEK_POSITION_END) < 0 ||
       !xe_cache_file_write_be32(file, words, 1057) || filestream_flush(file) ||
       xe_file_records_count(snapshot) != 1057 || !xe_file_records_get(snapshot, 1056)) return 32;
   xe_file_records_close(snapshot);
   if (!xe_cache_file_repair(file, 64)) return 31;
   filestream_close(file);
#ifdef XE_TEST_SHADER_STORAGE
   file = xe_cache_file_open(argv[1]);
   if (!file || !xe_cache_file_repair(file, 0)) return 25;
   memset(record, 0, sizeof(record));
   memset(record + 16, 0xFF, 4);
   if (xe_cache_file_write(record, 1, sizeof(record), file) != sizeof(record) ||
       filestream_seek(file, 8, RETRO_VFS_SEEK_POSITION_START) < 0 ||
       xe_test_shader_entries(file, &entries) != 8 || entries) return 26;
   if (!xe_cache_file_repair(file, 0)) return 27;
   hash = xxh3_64(encoded, sizeof(encoded), 0); words_count = 1057;
   memcpy(record + 8, &hash, sizeof(hash));
   memcpy(record + 16, &words_count, sizeof(words_count));
   if (xe_cache_file_write(record, 1, sizeof(record), file) != sizeof(record) ||
       !xe_cache_file_write_be32(file, words, 1057) ||
       filestream_seek(file, 8, RETRO_VFS_SEEK_POSITION_START) < 0 ||
       xe_test_shader_entries(file, &entries) != 20 + sizeof(encoded) || entries != 1) return 28;
   filestream_close(file);
#endif
   if (!validation_test(argv[1])) return 34;
   if (!writer_file_test(argv[1])) return 36;
   remove(argv[1]);
   puts("PASS: VFS short/EINTR transfers, repair/append position, errors, no truncating retry, bounds, native reopen and async split-record persistence");
   return 0;
}
