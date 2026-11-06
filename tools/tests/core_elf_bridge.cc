/* Actual heap/ElfModule interface checks; no guest execution or renderer. */
#include "xenia/cpu/elf_module.h"
#include "xenia/cpu/processor.h"
#include "xenia/base/logging.h"
#include <cstring>
#include <cstdio>
static int fixture_failure(int line) { std::fprintf(stderr, "ELF bridge line %d\n", line); return 0; }
extern "C" void xe_test_elf_tail_image(void);
extern "C" int xe_test_elf_bridge(const uint8_t* image, size_t size) {
  xe::InitializeLogging("elf_loader_test");
  xe::Memory memory;
  if (!memory.Initialize()) return fixture_failure(__LINE__);
  xe::cpu::Processor processor(&memory, nullptr);
  auto* heap = memory.LookupHeap(0x80000000);
  const uint32_t initial = heap->unreserved_page_count();
  if (!heap->AllocFixed(0x80060000, 65536, 65536,
          xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
          xe::kMemoryProtectRead | xe::kMemoryProtectWrite)) return fixture_failure(__LINE__);
  std::memset(memory.TranslateVirtual(0x80060000), 0xd4, 65536);
  {
    xe::cpu::ElfModule no_boot_loan(&processor, nullptr);
    if (no_boot_loan.Load("fixture", "fixture", image, size)) return fixture_failure(__LINE__);
  }
  {
    xe::cpu::ElfModule failed(&processor, nullptr);
    if (failed.loaded() || failed.is_executable() || failed.entry_point()) return fixture_failure(__LINE__);
    if (failed.Load("fixture", "fixture", image, size, true) || failed.loaded() ||
        failed.is_executable() || failed.entry_point()) return fixture_failure(__LINE__);
  }
  if (heap->unreserved_page_count() != initial - 1) {
    std::fprintf(stderr, "heap initial=%u current=%u\n", initial, heap->unreserved_page_count());
    return fixture_failure(__LINE__);
  }
  const uint8_t* protected_bytes = memory.TranslateVirtual(0x80060000);
  for (size_t i = 0; i < 65536; ++i) if (protected_bytes[i] != 0xd4) return fixture_failure(__LINE__);
  if (!heap->Release(0x80060000)) return fixture_failure(__LINE__);
  {
    xe::cpu::ElfModule module(&processor, nullptr);
    /* Fixture intentionally has no PF_X segments: no JIT/backend is created. */
    if (!module.Load("fixture", "fixture", image, size, true) || !module.loaded() ||
        !module.is_executable() || module.entry_point() != 0x80000010) return fixture_failure(__LINE__);
    if (module.Load("again", "again", image, size)) return fixture_failure(__LINE__);
    if (std::memcmp(memory.TranslateVirtual(0x80000010), image + 512, 31) ||
        std::memcmp(memory.TranslateVirtual(0x80040000), image + 600, 17) ||
        std::memcmp(memory.TranslateVirtual(0x80060000), image + 700, 33)) return fixture_failure(__LINE__);
    for (unsigned i = 0; i < 9; ++i)
      if (memory.TranslateVirtual(0x8000002f)[i]) return fixture_failure(__LINE__);
    if (heap->unreserved_page_count() != initial - 2) return fixture_failure(__LINE__);
  }
  if (!heap->Release(0x80040000) || !heap->Release(0x80060000)) return fixture_failure(__LINE__);
  if (heap->unreserved_page_count() != initial) return fixture_failure(__LINE__);
  /* A single page union includes both lent bootstrap and newly reserved tail. */
  xe_test_elf_tail_image();
  {
    xe::cpu::ElfModule tail(&processor, nullptr);
    if (!tail.Load("tail", "tail", image, size, true) ||
        heap->unreserved_page_count() != initial - 1) return fixture_failure(__LINE__);
  }
  if (!heap->Release(0x80040000)) return fixture_failure(__LINE__);
  xe::HeapAllocationInfo boot{};
  if (!heap->QueryRegionInfo(0x80000000, &boot, 0x40000) ||
      boot.allocation_base != 0x80000000 || boot.allocation_size != 0x40000)
    return fixture_failure(__LINE__);
  return heap->unreserved_page_count() == initial;
}
