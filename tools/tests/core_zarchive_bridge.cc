/* Minimal C ABI bridge to the existing archive classes. */
#include <cstdio>
#include "zarchive/zarchivewriter.h"
#include "zarchive/zarchivereader.h"
struct writer_state { FILE *file; ZArchiveWriter *writer; };
static void new_output(int32_t part, void *opaque) { (void)part; (void)opaque; }
static void write_output(const void *data, size_t length, void *opaque) {
  writer_state *state = static_cast<writer_state *>(opaque);
  fwrite(data, 1, length, state->file);
}
extern "C" void *archive_test_writer(FILE *file) {
  writer_state *state = new writer_state{file, nullptr};
  state->writer = new ZArchiveWriter(new_output, write_output, state);
  return state;
}
extern "C" int archive_test_write(void *opaque, const char *name, const void *data, size_t size) {
  writer_state *state = static_cast<writer_state *>(opaque);
  if (!state->writer->StartNewFile(name)) return 0;
  state->writer->AppendData(data, size);
  return !ferror(state->file);
}
extern "C" int archive_test_mkdir(void *opaque, const char *name) {
  return static_cast<writer_state *>(opaque)->writer->MakeDir(name, true);
}
extern "C" void archive_test_finish(void *opaque) {
  writer_state *state = static_cast<writer_state *>(opaque);
  state->writer->Finalize(); delete state->writer; delete state;
}
extern "C" void *archive_test_open(const char *path) { return ZArchiveReader::OpenFromFile(path); }
extern "C" uint64_t archive_test_read(void *opaque, const char *name, uint64_t offset, size_t size, void *output) {
  ZArchiveReader *reader = static_cast<ZArchiveReader *>(opaque);
  ZArchiveNodeHandle node = reader->LookUp(name);
  if (node == ZARCHIVE_INVALID_NODE) return 0;
  return reader->ReadFromFile(node, offset, size, output);
}
extern "C" void archive_test_close(void *opaque) { delete static_cast<ZArchiveReader *>(opaque); }
