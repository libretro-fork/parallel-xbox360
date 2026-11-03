/* Minimal interoperability test for the existing C++ result interface. */
#include "xenia/vfs/xex_metadata.h"
#include <cstring>

extern "C" int xe_test_xex_projection(const uint8_t* data, size_t size) {
  auto mapped = xe::vfs::ExtractXexMetadata(data, size);
  if (!mapped) return 0;
  xe_xex_source source = {};
  source.context = const_cast<uint8_t*>(data);
  source.size = size;
  source.read = [](void* p, uint64_t offset, void* out, size_t length) -> int {
    std::memcpy(out, static_cast<uint8_t*>(p) + size_t(offset), length);
    return 1;
  };
  auto streamed = xe::vfs::ExtractXexMetadata(source);
  if (!streamed) return 0;
  return mapped->format == xe::vfs::XexFormat::kXex2 &&
      mapped->module_name == std::string("a\0bc", 4) &&
      streamed->module_name == mapped->module_name &&
      mapped->title_id == 456 && streamed->title_id == 456 &&
      mapped->media_id == 123 && streamed->media_id == 123 &&
      mapped->savegame_id == 789 && streamed->savegame_id == 789 &&
      mapped->version.value() == 0x12345678 &&
      streamed->base_version.value() == 0x87654321U &&
      mapped->disc_number == 1 && streamed->disc_count == 2 &&
      mapped->ratings.present && streamed->ratings.present &&
      mapped->ratings.esrb == 7 && streamed->ratings.pegi == 8 &&
      mapped->ratings.cero == 9 && streamed->ratings.usk == 10 &&
      mapped->ratings.oflc_au == 11 && streamed->ratings.oflc_nz == 12;
}
