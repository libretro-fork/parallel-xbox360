/* Actual heap metadata and backing memory, without a renderer or guest workers. */
#include "xenia/memory.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)
int main() {
  xe::InitializeLogging("memory_state_test");
  xe::Memory memory;
  CHECK(memory.Initialize());
  const uint32_t virt = memory.SystemHeapAlloc(8192);
  const uint32_t phys = memory.SystemHeapAlloc(8192, 4096, xe::kSystemHeapPhysical);
  CHECK(virt && phys);
  std::memset(memory.TranslateVirtual(virt), 0x37, 8192);
  std::memset(memory.TranslateVirtual(phys), 0xB6, 8192);
  const uint32_t free_before = memory.LookupHeap(virt)->unreserved_page_count();
  xe::ByteStream count(nullptr, SIZE_MAX, 0, true);
  CHECK(memory.Save(&count));
  auto* bytes = static_cast<unsigned char*>(std::malloc(count.offset()));
  CHECK(bytes);
  xe::ByteStream save(bytes, count.offset());
  CHECK(memory.Save(&save));
  CHECK(save.offset() == count.offset());
  std::memset(memory.TranslateVirtual(virt), 0x91, 8192);
  std::memset(memory.TranslateVirtual(phys), 0x14, 8192);
  memory.SystemHeapFree(virt);
  memory.SystemHeapFree(phys);
  xe::ByteStream restore(bytes, save.offset());
  CHECK(memory.Restore(&restore));
  CHECK(restore.remaining() == 0);
  CHECK(memory.LookupHeap(virt)->unreserved_page_count() == free_before);
  for (unsigned i = 0; i < 8192; ++i) {
    CHECK(memory.TranslateVirtual(virt)[i] == 0x37);
    CHECK(memory.TranslateVirtual(phys)[i] == 0xB6);
  }
  memory.SystemHeapFree(virt);
  memory.SystemHeapFree(phys);
  std::free(bytes);
  std::puts("PASS: actual virtual/physical RAM and all physical-alias allocation tables restore after freeing their allocations");
}
