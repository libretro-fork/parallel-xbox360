/* Actual private parser exercise without frontend/backend/JIT setup. Explicit
 * template instantiation permits this member pointer under [temp.explicit]. */
#include "xenia/cpu/xex_module.h"
#include "xenia/cpu/processor.h"
#include "xenia/base/pe_image.h"
#include <cstring>
using PeModule = xe::cpu::XexModule;
struct PeReadTag {
  using type = int (PeModule::*)();
  friend type pe_read(PeReadTag);
};
template<class Tag, typename Tag::type Member> struct PeReadMember {
  friend typename Tag::type pe_read(Tag) { return Member; }
};
template struct PeReadMember<PeReadTag, &PeModule::ReadPEHeaders>;
struct PeAnalyzeTag {
  using type = std::vector<uint32_t> (PeModule::*)();
  friend type pe_read(PeAnalyzeTag);
};
template struct PeReadMember<PeAnalyzeTag, &PeModule::PreanalyzeCode>;
static_assert(sizeof(PESection) == sizeof(xe_pe_section));
static_assert(sizeof(PESection) == 32 && offsetof(PESection, raw_address) == 12);
static_assert(sizeof(XIMAGE_SECTION_HEADER) == 40);
static_assert(sizeof(XIMAGE_NT_HEADERS32) == 248);
static_assert(offsetof(XIMAGE_DOS_HEADER, e_lfanew) == 60);
static_assert(offsetof(XIMAGE_OPTIONAL_HEADER32, Subsystem) + 24 == 92);
extern "C" void xe_test_prepare_delta(int, int, const uint8_t**, size_t*,
    const uint8_t**, size_t*, const uint8_t**);
extern "C" int xe_test_pe_module(const uint8_t* pe, size_t size,
    const xe_pe_section* expected) {
  const uint8_t *base, *patch, *unused;
  size_t base_length, patch_length;
  xe_test_prepare_delta(0, 0, &base, &base_length, &patch, &patch_length, &unused);
  xe::Memory memory;
  if (!memory.Initialize()) return 0;
  xe::cpu::Processor processor(&memory, nullptr);
  PeModule module(&processor, nullptr);
  if (!module.Load("fixture", "fixture", base, base_length) || size != module.image_size()) return 0;
  uint8_t* guest = memory.TranslateVirtual(0x82000000);
  std::memcpy(guest, pe, size);
  const auto read = pe_read(PeReadTag{});
  if (!module.is_valid_executable() || (module.*read)()) return 0;
  const auto* text = module.GetPESection(".text");
  const auto* second = module.GetPESection("eightchr");
  if (!text || !second || module.GetPESection(nullptr) || module.GetPESection("missing")) return 0;
  if (text->address != expected[0].address || text->size != expected[0].size ||
      text->raw_size != expected[0].raw_size || text->raw_address != expected[0].raw_address ||
      text->flags != expected[0].flags || second->address != expected[1].address) return 0;
  std::memset(guest + 312, 0, 8);
  if (module.GetPESection(".text") != text) return 0;
  std::memcpy(guest, pe, size);
  if ((module.*read)() || module.GetPESection(".text") != text) return 0;
  std::memset(guest + 364, 255, 4);
  if (!(module.*read)() || module.GetPESection(".text")) return 0;
  if (module.LoadContinue() || module.LoadContinue()) return 0;
  std::memcpy(guest, pe, size); guest[70] = guest[71] = 0;
  if ((module.*read)() || !(module.*pe_read(PeAnalyzeTag{}))().empty()) return 0;
  std::memcpy(guest, pe, size);
  if ((module.*read)() || !module.Unload() || module.GetPESection(".text") ||
      module.is_valid_executable()) return 0;
  return 1;
}
