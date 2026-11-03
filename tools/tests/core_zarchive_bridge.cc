/* Minimal C ABI bridge to the existing archive classes. */
#include <cstdio>
#include <cstring>
#ifdef XE_ZAR_BASELINE
#include "zarchive/zarchivereader.h"
#else
#include "zarchive/zarchivewriter.h"
#include "libretro/core_zarchive.h"
#endif
#ifndef XE_ZAR_BASELINE
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
#endif
#ifdef XE_ZAR_BASELINE
extern "C" void *archive_test_open(const char *path) { return ZArchiveReader::OpenFromFile(path); }
extern "C" uint64_t archive_test_read(void *opaque, const char *name, uint64_t offset, size_t size, void *output) {
  ZArchiveReader *reader = static_cast<ZArchiveReader *>(opaque);
  ZArchiveNodeHandle node = reader->LookUp(name);
  if (node == ZARCHIVE_INVALID_NODE) return 0;
  return reader->ReadFromFile(node, offset, size, output);
}
extern "C" void archive_test_close(void *opaque) { delete static_cast<ZArchiveReader *>(opaque); }
extern "C" uint64_t archive_test_size(void *opaque, const char *name) {
  ZArchiveReader *reader = static_cast<ZArchiveReader *>(opaque);
  return reader->GetFileSize(reader->LookUp(name));
}
extern "C" uint32_t archive_test_lookup(void *opaque, const char *name) {
  return static_cast<ZArchiveReader *>(opaque)->LookUp(name);
}
extern "C" uint64_t archive_test_read_node(void *opaque, uint32_t node, uint64_t offset, size_t size, void *output) {
  return static_cast<ZArchiveReader *>(opaque)->ReadFromFile(node, offset, size, output);
}

#else
extern "C" void *archive_test_open(const char *path) { return xe_zar_open(path); }
extern "C" uint64_t archive_test_read(void *opaque, const char *name, uint64_t offset, size_t size, void *output) {
  xe_zar *reader = static_cast<xe_zar *>(opaque);
  return xe_zar_read(reader, xe_zar_lookup(reader, name, std::strlen(name), 1, 1), offset, size, output);
}
extern "C" void archive_test_close(void *opaque) { xe_zar_close(static_cast<xe_zar *>(opaque)); }
extern "C" uint64_t archive_test_size(void *opaque, const char *name) {
  xe_zar *reader = static_cast<xe_zar *>(opaque);
  return xe_zar_size(reader, xe_zar_lookup(reader, name, std::strlen(name), 1, 1));
}
extern "C" uint32_t archive_test_lookup(void *opaque, const char *name) {
  xe_zar *reader = static_cast<xe_zar *>(opaque);
  return xe_zar_lookup(reader, name, std::strlen(name), 1, 1);
}
extern "C" uint64_t archive_test_read_node(void *opaque, uint32_t node, uint64_t offset, size_t size, void *output) {
  return xe_zar_read(static_cast<xe_zar *>(opaque), node, offset, size, output);
}
#endif
