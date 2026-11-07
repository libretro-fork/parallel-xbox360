/* Actual heap metadata and backing memory, without a renderer or guest workers. */
#include "xenia/memory.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)
int main(int argc,char**) {
  const bool legacy=argc>1;
  xe::InitializeLogging("memory_state_test");
  xe::Memory memory;
  CHECK(memory.Initialize());
  const uint32_t virt = memory.SystemHeapAlloc(8192);
  const uint32_t phys = memory.SystemHeapAlloc(8192, 4096, xe::kSystemHeapPhysical);
  CHECK(virt && phys);
  std::memset(memory.TranslateVirtual(virt), 0x37, 8192);
  std::memset(memory.TranslateVirtual(phys), 0xB6, 8192);
  CHECK(memory.LookupHeap(virt)->Protect(virt+4096,4096,xe::kMemoryProtectRead,nullptr));
  const uint32_t free_before = memory.LookupHeap(virt)->unreserved_page_count();
  xe::ByteStream count(nullptr, SIZE_MAX, 0, true);
  CHECK(memory.Save(&count));
  auto* bytes = static_cast<unsigned char*>(std::malloc(count.offset()));
  CHECK(bytes);
  xe::ByteStream save(bytes, count.offset());
  CHECK(memory.Save(&save));
  CHECK(save.offset() == count.offset());
  CHECK(memory.LookupHeap(virt)->Protect(virt,8192,xe::kMemoryProtectRead|xe::kMemoryProtectWrite,nullptr));
  std::memset(memory.TranslateVirtual(virt), 0x91, 8192);
  std::memset(memory.TranslateVirtual(phys), 0x14, 8192);
  // A malformed final heap must not restore an earlier heap as a side effect.
  bool accepted=false;
  try { xe::ByteStream truncated(bytes,save.offset()-1);accepted=memory.Restore(&truncated); }
  catch(const xe::ByteStreamError&) {}
  CHECK(!accepted);
  CHECK(memory.TranslateVirtual(virt)[0]==(legacy?0x37:0x91));
  CHECK(memory.TranslateVirtual(phys)[0]==(legacy?0xB6:0x14));
  CHECK(memory.LookupHeap(virt)->Protect(virt,8192,xe::kMemoryProtectRead|xe::kMemoryProtectWrite,nullptr));
  std::memset(memory.TranslateVirtual(virt),0x91,8192);
  std::memset(memory.TranslateVirtual(phys),0x14,8192);
  xe::PageEntry original,damaged;
  std::memcpy(&original,bytes+save.offset()-sizeof(original),sizeof(original));
  damaged=original;damaged.reserved=1;
  std::memcpy(bytes+save.offset()-sizeof(damaged),&damaged,sizeof(damaged));
  xe::ByteStream malformed(bytes,save.offset());
  CHECK(!memory.Restore(&malformed));
  CHECK(memory.TranslateVirtual(virt)[0]==(legacy?0x37:0x91));
  CHECK(memory.TranslateVirtual(phys)[0]==(legacy?0xB6:0x14));
  CHECK(memory.LookupHeap(virt)->unreserved_page_count()==free_before);
  std::memcpy(bytes+save.offset()-sizeof(original),&original,sizeof(original));
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
  size_t protected_length=4096;
  xe::memory::PageAccess access;
  CHECK(xe::memory::QueryProtect(memory.TranslateVirtual(virt+4096),protected_length,access));
  CHECK(access==xe::memory::PageAccess::kReadOnly);
  memory.SystemHeapFree(virt);
  memory.SystemHeapFree(phys);
  std::free(bytes);
  std::puts(legacy?"PASS: archived restore reproduces partial RAM mutation on invalid late heap":"PASS: invalid late heap leaves live RAM intact; valid virtual/physical/alias state roundtrip");
}
