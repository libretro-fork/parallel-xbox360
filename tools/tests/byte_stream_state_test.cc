#include "xenia/base/byte_stream.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::exit(1); } } while (0)
int main() {
  unsigned char storage[64] = {};
  xe::ByteStream count(nullptr, SIZE_MAX, 0, true);
  count.Write(uint32_t(0));
  count.Write(std::string("checkpoint"));
  count.Write(std::u16string(u"guest"));
  uint32_t length = uint32_t(count.offset());
  count.Patch(0, &length, sizeof(length));
  xe::ByteStream writer(storage, count.offset());
  writer.Write(uint32_t(0));
  writer.Write(std::string("checkpoint"));
  writer.Write(std::u16string(u"guest"));
  writer.Patch(0, &length, sizeof(length));
  CHECK(writer.offset() == count.offset());
  xe::ByteStream reader(storage, writer.offset());
  CHECK(reader.Read<uint32_t>() == length);
  const auto borrowed = reader.ReadStringView();
  CHECK(borrowed == "checkpoint");
  CHECK(borrowed.data() == reinterpret_cast<const char*>(storage) + 8);
  CHECK(reader.Read<std::u16string>() == u"guest");
  CHECK(reader.remaining() == 0);
  bool rejected = false;
  try { reader.Read<uint32_t>(); } catch (const xe::ByteStreamError&) { rejected = true; }
  CHECK(rejected);
  for (size_t n = 0; n < writer.offset(); ++n) {
    xe::ByteStream truncated(storage, n);
    rejected = false;
    try { truncated.Read<uint32_t>(); truncated.Read<std::string>(); truncated.Read<std::u16string>(); }
    catch (const xe::ByteStreamError&) { rejected = true; }
    CHECK(rejected);
  }
  memset(storage, 0xFF, sizeof(uint32_t));
  xe::ByteStream malformed(storage, sizeof(storage));
  rejected = false;
  try { malformed.Read<std::u16string>(); } catch (const xe::ByteStreamError&) { rejected = true; }
  CHECK(rejected);
  storage[0] = 2;
  xe::ByteStream malformed_bool(storage, 1);
  rejected = false;
  try { malformed_bool.Read<bool>(); } catch (const xe::ByteStreamError&) { rejected = true; }
  CHECK(rejected);
  std::puts("PASS: actual ByteStream counts and roundtrips strings; every truncation and oversized length fails before allocation");
}
